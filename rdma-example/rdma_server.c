/*
 * RDMA 服务器端示例代码。
 * 演示了如何启动服务器、监听连接、预发布接收请求、接受连接以及与客户端交换元数据。
 */

#include "rdma_common.h" // 包含公共头文件，获取必要的结构定义、宏和辅助函数

/* --- RDMA 连接资源定义 --- */
// CM 事件通道：所有连接管理事件（如请求连接、连接已建立、已断开）都通过此通道汇报
static struct rdma_event_channel *cm_event_channel = NULL;
// cm_server_id 代表服务器的监听标识符；cm_client_id 代表当前已连接的特定客户端
static struct rdma_cm_id *cm_server_id = NULL, *cm_client_id = NULL;
// 保护域 (Protection Domain)：用于资源隔离的安全上下文
static struct ibv_pd *pd = NULL;
// I/O 完成通道：当数据传输完成时（CQ 中产生 WC），会通过此通道发出中断通知
static struct ibv_comp_channel *io_completion_channel = NULL;
// 完成队列 (CQ)：存放每个发送或接收操作的完成状态
static struct ibv_cq *cq = NULL;
// QP 初始化属性：定义队列对的容量
static struct ibv_qp_init_attr qp_init_attr;
// 客户端对应的队列对 (QP) 指针
static struct ibv_qp *client_qp = NULL;

/* --- 内存资源定义 --- */
// 分别代表：接收客户端元数据的 MR、提供给客户端读写的数据缓冲区 MR、以及发送服务器元数据的 MR
static struct ibv_mr *client_metadata_mr = NULL, *server_buffer_mr = NULL, *server_metadata_mr = NULL;
// 存储具体地址、长度和密钥的结构体
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
// 接收工作请求 (Recv WR) 及其错误指针
static struct ibv_recv_wr client_recv_wr, *bad_client_recv_wr = NULL;
// 发送工作请求 (Send WR) 及其错误指针
static struct ibv_send_wr server_send_wr, *bad_server_send_wr = NULL;
// 散集元素 (SGE)：描述物理内存的位置和大小
static struct ibv_sge client_recv_sge, server_send_sge;

/* 
 * 设置客户端连接所需的硬件资源。
 * 当收到 CONNECT_REQUEST 事件后，必须调用此函数为该连接准备 PD, CQ 和 QP。
 */
static int setup_client_resources()
{
	int ret = -1; // 返回值初始化
	if(!cm_client_id){ // 检查是否有有效的客户端 ID
		rdma_error("Client id is still NULL \n");
		return -EINVAL;
	}
	
	/* 1. 分配保护域 (PD)。
	 * 它是 RDMA 资源的逻辑容器。
	 * 使用 cm_client_id->verbs 获取关联的底层设备上下文。
	 */
	pd = ibv_alloc_pd(cm_client_id->verbs);
	if (!pd) { // 检查分配是否成功
		rdma_error("Failed to allocate a protection domain errno: %d\n",
				-errno);
		return -errno;
	}
	debug("A new protection domain is allocated at %p \n", pd);
	
	/* 2. 创建 I/O 完成通道。
	 * 用于监听 CQ 中的完成事件（WC）。
	 */
	io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
	if (!io_completion_channel) {
		rdma_error("Failed to create an I/O completion event channel, %d\n",
				-errno);
		return -errno;
	}
	debug("An I/O completion event channel is created at %p \n", 
			io_completion_channel);
			
	/* 3. 创建完成队列 (CQ)。
	 * 存放 ibv_wc 完成条目。
	 * 容量设为 CQ_CAPACITY，并绑定到刚才创建的通知通道。
	 */
	cq = ibv_create_cq(cm_client_id->verbs, 
			CQ_CAPACITY, 
			NULL, // 用户定义上下文
			io_completion_channel, 
			0); // 信号向量，通常设为 0
	if (!cq) {
		rdma_error("Failed to create a completion queue (cq), errno: %d\n",
				-errno);
		return -errno;
	}
	debug("Completion queue (CQ) is created at %p with %d elements \n", 
			cq, cq->cqe);
			
	/* 4. 开启通知机制。
	 * 申请在 CQ 中产生新完成消息时触发事件通知。
	 */
	ret = ibv_req_notify_cq(cq, 
			0); // 0 表示所有类型的完成事件都触发
	if (ret) {
		rdma_error("Failed to request notifications on CQ errno: %d \n",
				-errno);
		return -errno;
	}
	
	/* 5. 配置并创建队列对 (QP)。
	 * QP 是 RDMA 通信的双向端点。
	 */
       bzero(&qp_init_attr, sizeof qp_init_attr); // 内存清零
       qp_init_attr.cap.max_recv_sge = MAX_SGE; // 最大接收离散段
       qp_init_attr.cap.max_recv_wr = MAX_WR; // 接收队列深度
       qp_init_attr.cap.max_send_sge = MAX_SGE; // 最大发送离散段
       qp_init_attr.cap.max_send_wr = MAX_WR; // 发送队列深度
       qp_init_attr.qp_type = IBV_QPT_RC; // 模式：可靠连接 (Reliable Connection)
       qp_init_attr.recv_cq = cq; // 绑定接收 CQ
       qp_init_attr.send_cq = cq; // 绑定发送 CQ
       
       /* 调用连接管理器 API 创建 QP。 */
       ret = rdma_create_qp(cm_client_id,
		       pd,
		       &qp_init_attr);
       if (ret) {
	       rdma_error("Failed to create QP due to errno: %d\n", -errno);
	       return -errno;
       }
       client_qp = cm_client_id->qp; // 从 cm_id 中提取生成的 QP
       debug("Client QP created at %p\n", client_qp);
       return ret; // 返回 0 表示成功
}

/* 
 * 启动服务器：创建监听通道并开始监听端口。
 */
static int start_rdma_server(struct sockaddr_in *server_addr) 
{
	struct rdma_cm_event *cm_event = NULL; // 临时存储捕获的连接事件
	int ret = -1;
	
	/* 1. 创建 CM 事件通道。 */
	cm_event_channel = rdma_create_event_channel();
	if (!cm_event_channel) {
		rdma_error("Creating cm event channel failed with errno : (%d)", -errno);
		return -errno;
	}
	debug("RDMA CM event channel is created successfully at %p \n", 
			cm_event_channel);
			
	/* 2. 创建服务器监听标识符。
	 * 类似于 TCP server 的 listen fd。
	 */
	ret = rdma_create_id(cm_event_channel, &cm_server_id, NULL, RDMA_PS_TCP);
	if (ret) {
		rdma_error("Creating server cm id failed with errno: %d ", -errno);
		return -errno;
	}
	debug("A RDMA connection id for the server is created \n");
	
	/* 3. 地址绑定。将 CM ID 绑定到具体的本地 IP 和端口。 */
	ret = rdma_bind_addr(cm_server_id, (struct sockaddr*) server_addr);
	if (ret) {
		rdma_error("Failed to bind server address, errno: %d \n", -errno);
		return -errno;
	}
	debug("Server RDMA CM id is successfully binded \n");
	
	/* 4. 开始监听连接请求。
	 * 参数 8 是 backlog（待处理队列深度）。这是一个非阻塞操作。
	 */
	ret = rdma_listen(cm_server_id, 8); 
	if (ret) {
		rdma_error("rdma_listen failed to listen on server address, errno: %d ",
				-errno);
		return -errno;
	}
	printf("Server is listening successfully at: %s , port: %d \n",
			inet_ntoa(server_addr->sin_addr),
			ntohs(server_addr->sin_port));
			
	/* 5. 阻塞等待：等待第一个客户端发起连接请求。 */
	ret = process_rdma_cm_event(cm_event_channel, 
			RDMA_CM_EVENT_CONNECT_REQUEST, // 核心：此时期望收到连接请求
			&cm_event);
	if (ret) {
		rdma_error("Failed to get cm event, ret = %d \n" , ret);
		return ret;
	}
	
	/* 6. 保存新创建的客户端 CM ID。
	 * 监听成功后，event->id 会指向一个新的标识符，专门用于与该客户端通信。
	 */
	cm_client_id = cm_event->id;
	
	/* 7. 确认事件以释放相关内存。 */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge the cm event errno: %d \n", -errno);
		return -errno;
	}
	debug("A new RDMA client connection id is stored at %p\n", cm_client_id);
	return ret; // 启动成功
}

/* 
 * 预发布接收缓冲区并接受连接。
 * 在 RDMA 编程中，必须在接受连接前先挂好一个接收槽位，以保证不丢失对方发来的第一条数据。
 */
static int accept_client_connection()
{
	struct rdma_conn_param conn_param; // 连接协商参数
	struct rdma_cm_event *cm_event = NULL;
	struct sockaddr_in remote_sockaddr; // 存储对端地址
	int ret = -1;
	
	// 验证资源是否已就绪
	if(!cm_client_id || !client_qp) {
		rdma_error("Client resources are not properly setup\n");
		return -EINVAL;
	}
	
	/* 1. 注册接收缓冲区 MR。该内存将用于存放客户端发来的元数据。 */
        client_metadata_mr = rdma_buffer_register(pd, 
			&client_metadata_attr, // 这是一个本地结构体，将被对端发来的数据覆盖
			sizeof(client_metadata_attr), 
		       (IBV_ACCESS_LOCAL_WRITE)); // 授予网卡写的权限
	if(!client_metadata_mr){
		rdma_error("Failed to register client attr buffer\n");
		return -ENOMEM;
	}
	
	/* 2. 配置 SGE。 */
	client_recv_sge.addr = (uint64_t) client_metadata_mr->addr; 
	client_recv_sge.length = client_metadata_mr->length;
	client_recv_sge.lkey = client_metadata_mr->lkey;
	
	/* 3. 构造接收工作请求 (Recv WR)。 */
	bzero(&client_recv_wr, sizeof(client_recv_wr));
	client_recv_wr.sg_list = &client_recv_sge;
	client_recv_wr.num_sge = 1;
	
	/* 4. 发布接收请求到接收队列。硬件此时做好了“接球”准备。 */
	ret = ibv_post_recv(client_qp,
		      &client_recv_wr,
		      &bad_client_recv_wr);
	if (ret) {
		rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
		return ret;
	}
	debug("Receive buffer pre-posting is successful \n");
	
	/* 5. 调用 rdma_accept 真正接受连接。 */
       memset(&conn_param, 0, sizeof(conn_param));
       conn_param.initiator_depth = 3; // 对应客户端发起的 READ/ATOMIC 容量
       conn_param.responder_resources = 3; // 对应本端响应的资源深度
       ret = rdma_accept(cm_client_id, &conn_param);
       if (ret) {
	       rdma_error("Failed to accept the connection, errno: %d \n", -errno);
	       return -errno;
       }
       
       /* 6. 等待连接完全就绪。握手包交换完成后，会产生 ESTABLISHED 事件。 */
        debug("Going to wait for : RDMA_CM_EVENT_ESTABLISHED event \n");
       ret = process_rdma_cm_event(cm_event_channel, 
		       RDMA_CM_EVENT_ESTABLISHED,
		       &cm_event);
        if (ret) {
		rdma_error("Failed to get the cm event, errnp: %d \n", -errno);
		return -errno;
	}
	
	/* 7. 确认事件。 */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge the cm event %d\n", -errno);
		return -errno;
	}
	
	/* 8. 辅助操作：获取对端的套接字信息并打印其 IP 地址。 */
	memcpy(&remote_sockaddr, 
			rdma_get_peer_addr(cm_client_id), 
			sizeof(struct sockaddr_in));
	printf("A new connection is accepted from %s \n", 
			inet_ntoa(remote_sockaddr.sin_addr));
	return ret; // 接受成功
}

/* 
 * 将服务器端的内存元数据发送给客户端。
 * 流程：
 * 1. 等待客户端发送其元数据（表明它准备好了）。
 * 2. 准备本端的读写缓冲区。
 * 3. 将本端的 RKey 和地址信息发送给客户端。
 */
static int send_server_metadata_to_client() 
{
	struct ibv_wc wc; // 用于接收完成状态
	int ret = -1;
	
	/* 1. 阻塞等待：收割客户端发来的元数据通知。
	 * 这对应于我们之前在 accept 函数中 pre-post 的 RECV。
	 */
	ret = process_work_completion_events(io_completion_channel, &wc, 1);
	if (ret != 1) {
		rdma_error("Failed to receive , ret = %d \n", ret);
		return ret;
	}
	/* 如果成功，client_metadata_attr 现在包含了客户端的 MR 凭证。 */
	printf("Client side buffer information is received...\n");
	show_rdma_buffer_attr(&client_metadata_attr);
	printf("The client has requested buffer length of : %u bytes \n", 
			client_metadata_attr.length);
			
	/* 2. 按需分配数据缓冲区。
	 * 这是一个供客户端进行 RDMA READ/WRITE 的实际业务数据内存。
	 * 权限必须包含 REMOTE_READ 和 REMOTE_WRITE。
	 */
       server_buffer_mr = rdma_buffer_alloc(pd, 
		       client_metadata_attr.length, // 根据客户端请求的大小分配
		       (IBV_ACCESS_LOCAL_WRITE|
		       IBV_ACCESS_REMOTE_READ|
		       IBV_ACCESS_REMOTE_WRITE));
       if(!server_buffer_mr){
	       rdma_error("Server failed to create a buffer \n");
	       return -ENOMEM;
       }
       
       /* 3. 准备服务器的元数据属性包。 */
       server_metadata_attr.address = (uint64_t) server_buffer_mr->addr;
       server_metadata_attr.length = (uint32_t) server_buffer_mr->length;
       server_metadata_attr.stag.local_stag = (uint32_t) server_buffer_mr->lkey;
       
       /* 4. 注册元数据缓冲区本身，以便将其发送出去。 */
       server_metadata_mr = rdma_buffer_register(pd, 
		       &server_metadata_attr, 
		       sizeof(server_metadata_attr), 
		       IBV_ACCESS_LOCAL_WRITE);
       if(!server_metadata_mr){
	       rdma_error("Server failed to create to hold server metadata \n");
	       return -ENOMEM;
       }
       
       /* 5. 构造并发布发送工作请求。这是一个 RDMA SEND (双边传输)。 */
       server_send_sge.addr = (uint64_t) &server_metadata_attr;
       server_send_sge.length = sizeof(server_metadata_attr);
       server_send_sge.lkey = server_metadata_mr->lkey;
       
       bzero(&server_send_wr, sizeof(server_send_wr));
       server_send_wr.sg_list = &server_send_sge;
       server_send_wr.num_sge = 1;
       server_send_wr.opcode = IBV_WR_SEND; // 核心：使用 SEND 操作码
       server_send_wr.send_flags = IBV_SEND_SIGNALED; 
       
       /* 发布发送。 */
       ret = ibv_post_send(client_qp, 
		       &server_send_wr, 
		       &bad_server_send_wr);
       if (ret) {
	       rdma_error("Posting of server metdata failed, errno: %d \n",
			       -errno);
	       return -errno;
       }
       
       /* 6. 等待服务器元数据成功发出的确认。 */
       ret = process_work_completion_events(io_completion_channel, &wc, 1);
       if (ret != 1) {
	       rdma_error("Failed to send server metadata, ret = %d \n", ret);
	       return ret;
       }
       debug("Local buffer metadata has been sent to the client \n");
       return 0; // 交换成功
}

/* 
 * 被动断开逻辑。
 * 服务器端只需等待客户端发起断开请求，然后释放自身资源。
 */
static int disconnect_and_cleanup()
{
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;
	
       /* 1. 等待断开事件 (DISCONNECTED)。客户端调用 rdma_disconnect 后会产生此事件。 */
       debug("Waiting for cm event: RDMA_CM_EVENT_DISCONNECTED\n");
       ret = process_rdma_cm_event(cm_event_channel, 
		       RDMA_CM_EVENT_DISCONNECTED, 
		       &cm_event);
       if (ret) {
	       rdma_error("Failed to get disconnect event, ret = %d \n", ret);
	       return ret;
       }
	/* 2. 确认事件。 */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge the cm event %d\n", -errno);
		return -errno;
	}
	printf("A disconnect event is received from the client...\n");
	
	/* --- 依次清理所有硬件资源 --- */
	/* 3. 销毁客户端对应的 QP。 */
	rdma_destroy_qp(cm_client_id);
	/* 4. 销毁客户端对应的连接标识符。 */
	ret = rdma_destroy_id(cm_client_id);
	if (ret) {
		rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
	}
	/* 5. 销毁完成队列。 */
	ret = ibv_destroy_cq(cq);
	if (ret) {
		rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
	}
	/* 6. 销毁 I/O 通知通道。 */
	ret = ibv_destroy_comp_channel(io_completion_channel);
	if (ret) {
		rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
	}
	/* 7. 销毁所有内存缓冲区并注销 MR。 */
	rdma_buffer_free(server_buffer_mr);
	rdma_buffer_deregister(server_metadata_mr);	
	rdma_buffer_deregister(client_metadata_mr);	
	/* 8. 销毁保护域。 */
	ret = ibv_dealloc_pd(pd);
	if (ret) {
		rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
	}
	/* 9. 最后销毁服务器自身的监听 ID 和 CM 事件通道。 */
	ret = rdma_destroy_id(cm_server_id);
	if (ret) {
		rdma_error("Failed to destroy server id cleanly, %d \n", -errno);
	}
	rdma_destroy_event_channel(cm_event_channel);
	printf("Server shut-down is complete \n");
	return 0; // 关机成功
}

/* 打印使用方法。 */
void usage() 
{
	printf("Usage:\n");
	printf("rdma_server: [-a <server_addr>] [-p <server_port>]\n");
	printf("(default port is %d)\n", DEFAULT_RDMA_PORT);
	exit(1);
}

/* 
 * 主函数：解析参数并驱动服务器的生命周期。
 */
int main(int argc, char **argv) 
{
	int ret, option;
	struct sockaddr_in server_sockaddr; // 服务器套接字地址
	
	bzero(&server_sockaddr, sizeof server_sockaddr); // 清零
	server_sockaddr.sin_family = AF_INET; // IPv4
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY); // 默认监听所有网卡 (0.0.0.0)
	
	/* 命令行参数解析。 */
	while ((option = getopt(argc, argv, "a:p:")) != -1) {
		switch (option) {
			case 'a': // 解析特定 IP
				ret = get_addr(optarg, (struct sockaddr*) &server_sockaddr);
				if (ret) {
					rdma_error("Invalid IP \n");
					 return ret;
				}
				break;
			case 'p': // 解析端口
				server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0)); 
				break;
			default: // 打印帮助
				usage();
				break;
		}
	}
	// 检查端口。如果未指定，则使用默认端口。
	if(!server_sockaddr.sin_port) {
		server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
	 }
	 
	/* --- 执行步骤 --- */
	
	/* 1. 启动服务器并开始监听。阻塞直到有客户端申请连接。 */
	ret = start_rdma_server(&server_sockaddr);
	if (ret) {
		rdma_error("RDMA server failed to start cleanly, ret = %d \n", ret);
		return ret;
	}
	
	/* 2. 准备硬件资源 (PD, CQ, QP)。 */
	ret = setup_client_resources();
	if (ret) { 
		rdma_error("Failed to setup client resources, ret = %d \n", ret);
		return ret;
	}
	
	/* 3. 预发布接收缓冲区并正式接受连接。 */
	ret = accept_client_connection();
	if (ret) {
		rdma_error("Failed to handle client cleanly, ret = %d \n", ret);
		return ret;
	}
	
	/* 4. 交换元数据：服务器向客户端公布自身的内存凭证以便被访问。 */
	ret = send_server_metadata_to_client();
	if (ret) {
		rdma_error("Failed to send server metadata to the client, ret = %d \n", ret);
		return ret;
	}
	
	/* 5. 等待客户端断开并完成最后清理。 */
	ret = disconnect_and_cleanup();
	if (ret) { 
		rdma_error("Failed to clean up resources properly, ret = %d \n", ret);
		return ret;
	}
	
	return 0; // 程序圆满结束
}
