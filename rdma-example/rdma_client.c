/*
 * RDMA 客户端示例代码。
 * 演示了如何主动发起连接，交换内存信息，并执行单边 RDMA 操作。
 */

#include "rdma_common.h" // 包含公共头文件，获取所有必要的 RDMA 结构和函数声明

/* --- 基础 RDMA 连接资源 --- */
// 用于接收连接管理器 (CM) 事件的通道，如地址解析完成、连接成功等
static struct rdma_event_channel *cm_event_channel = NULL;
// 客户端的连接标识符，类似于 socket 句柄
static struct rdma_cm_id *cm_client_id = NULL;
// 保护域 (Protection Domain)，所有 RDMA 资源（QP, MR 等）的容器，用于安全隔离
static struct ibv_pd *pd = NULL;
// I/O 完成通道，当完成队列 (CQ) 中有新消息时，网卡会通过此通道发出中断/事件通知
static struct ibv_comp_channel *io_completion_channel = NULL;
// 完成队列 (Completion Queue)，存放每个发送或接收操作的结果 (Work Completion)
static struct ibv_cq *client_cq = NULL;
// 队列对 (Queue Pair, QP) 的初始化属性，定义其容量和关联的 CQ
static struct ibv_qp_init_attr qp_init_attr;
// 具体的队列对指针，包含发送队列 (SQ) 和接收队列 (RQ)
static struct ibv_qp *client_qp;

/* --- 内存缓冲区相关资源 --- */
// 分别代表：本地元数据、源数据、目的数据以及存储远程服务器元数据的内存区域 (MR)
static struct ibv_mr *client_metadata_mr = NULL, 
		     *client_src_mr = NULL, 
		     *client_dst_mr = NULL, 
		     *server_metadata_mr = NULL;
// 存储具体地址、长度和密钥的结构体
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
// 发送工作请求 (Send Work Request) 及其错误指针
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
// 接收工作请求 (Recv Work Request) 及其错误指针
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
// 散集元素 (Scatter/Gather Element)，指向数据的实际物理位置
static struct ibv_sge client_send_sge, server_recv_sge;
static struct rdma_buffer_attr client_metadata_attr, server_metadata_attr;
// 发送工作请求 (Send Work Request) 及其错误指针
static struct ibv_send_wr client_send_wr, *bad_client_send_wr = NULL;
// 接收工作请求 (Recv Work Request) 及其错误指针
static struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = NULL;
// 散集元素 (Scatter/Gather Element)，指向数据的实际物理位置
static struct ibv_sge client_send_sge, server_recv_sge;
// src 指向发送的数据源，dst 指向读取后的存放目的地
static char *src = NULL, *dst = NULL; 

/* 
 * 校验函数：比较源缓冲区和目的缓冲区的内容。
 * 如果 RDMA READ 成功，dst 应该包含与 src 相同的数据。
 */
static int check_src_dst() 
{
	return memcmp((void*) src, (void*) dst, strlen(src));
}

/* 
 * 为 RDMA 连接准备客户端资源。
 * 包括创建事件通道、标识符、解析地址、创建 PD、CQ 和 QP。
 */
static int client_prepare_connection(struct sockaddr_in *s_addr)
{
	struct rdma_cm_event *cm_event = NULL; // 用于接收事件结果的临时指针
	int ret = -1; // 返回状态初始化
	
	/* 1. 创建事件通道。该通道异步报告所有 CM 相关的状态变化。 */
	cm_event_channel = rdma_create_event_channel();
	if (!cm_event_channel) { // 检查创建是否成功
		rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
		return -errno;
	}
	debug("RDMA CM event channel is created at : %p \n", cm_event_channel);
	
	/* 2. 创建 rdma_cm_id。这是 RDMA 连接的核心对象。
	 * 使用 RDMA_PS_TCP (TCP 端口空间) 模式，虽然是 RDMA 传输，但握手使用类似 TCP 的语义。
	 */
	ret = rdma_create_id(cm_event_channel, &cm_client_id, 
			NULL, // 用户上下文，此处不使用
			RDMA_PS_TCP);
	if (ret) { // 检查返回码
		rdma_error("Creating cm id failed with errno: %d \n", -errno); 
		return -errno;
	}
	
	/* 3. 解析目标地址。将输入的 IP 和端口转换为内部的 RDMA 地址。
	 * 2000 ms 是超时限制。此步骤会将 cm_client_id 绑定到一个具体的本地设备。
	 */
	ret = rdma_resolve_addr(cm_client_id, NULL, (struct sockaddr*) s_addr, 2000);
	if (ret) {
		rdma_error("Failed to resolve address, errno: %d \n", -errno);
		return -errno;
	}
	debug("waiting for cm event: RDMA_CM_EVENT_ADDR_RESOLVED\n");
	
	/* 4. 等待地址解析完成事件。rdma_resolve_addr 是非阻塞的，所以必须在此等待其结果。 */
	ret  = process_rdma_cm_event(cm_event_channel, 
			RDMA_CM_EVENT_ADDR_RESOLVED,
			&cm_event);
	if (ret) {
		rdma_error("Failed to receive a valid event, ret = %d \n", ret);
		return ret;
	}
	
	/* 5. 确认收到的 CM 事件。所有的 cm 事件在使用后都必须手动 Ack 以释放内存。 */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
		return -errno;
	}
	debug("RDMA address is resolved \n");

	 /* 6. 解析 RDMA 路由。在知道目标地址后，需要找到具体的链路层路由（如 IB 交换机路径）。 */
	ret = rdma_resolve_route(cm_client_id, 2000);
	if (ret) {
		rdma_error("Failed to resolve route, erno: %d \n", -errno);
	       return -errno;
	}
	debug("waiting for cm event: RDMA_CM_EVENT_ROUTE_RESOLVED\n");
	
	/* 7. 等待路由解析完成。 */
	ret = process_rdma_cm_event(cm_event_channel, 
			RDMA_CM_EVENT_ROUTE_RESOLVED,
			&cm_event);
	if (ret) {
		rdma_error("Failed to receive a valid event, ret = %d \n", ret);
		return ret;
	}
	
	/* 8. 确认路由解析事件。 */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
		return -errno;
	}
	printf("Trying to connect to server at : %s port: %d \n", 
			inet_ntoa(s_addr->sin_addr),
			ntohs(s_addr->sin_port));
			
	/* 9. 创建保护域 (PD)。
	 * 保护域类似于操作系统中的进程抽象，它把不同的 RDMA 资源绑在一起。
	 * 跨 PD 访问资源会导致硬件级保护故障。
	 */
	pd = ibv_alloc_pd(cm_client_id->verbs); // 使用 cm_id 关联的设备上下文分配 PD
	if (!pd) {
		rdma_error("Failed to alloc pd, errno: %d \n", -errno);
		return -errno;
	}
	debug("pd allocated at %p \n", pd);
	
	/* 10. 创建 I/O 完成通道。
	 * 该通道用于接收数据传输完成的通知。注意这和连接管理 (CM) 通道是完全不同的。
	 */
	io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
	if (!io_completion_channel) {
		rdma_error("Failed to create IO completion event channel, errno: %d\n",
			       -errno);
	return -errno;
	}
	debug("completion event channel created at : %p \n", io_completion_channel);
	
	/* 11. 创建完成队列 (CQ)。
	 * 实际的传输结果 (ibv_wc 结构) 会放入此队列。
	 * 参数包括设备、容量、完成通道。
	 */
	client_cq = ibv_create_cq(cm_client_id->verbs, 
			CQ_CAPACITY, 
			NULL, // 用户定义上下文
			io_completion_channel, 
			0); // 信号向量，此处不使用
	if (!client_cq) {
		rdma_error("Failed to create CQ, errno: %d \n", -errno);
		return -errno;
	}
	debug("CQ created at %p with %d elements \n", client_cq, client_cq->cqe);
	
	/* 12. 请求 CQ 的事件通知。
	 * 告诉 CQ：当有新的完成条目到达时，请通过 comp_channel 提醒我。
	 */
	ret = ibv_req_notify_cq(client_cq, 0);
	if (ret) {
		rdma_error("Failed to request notifications, errno: %d\n", -errno);
		return -errno;
	}
	
       /* 13. 配置并创建队列对 (QP)。
        * 队列对由发送队列和接收队列组成。
        */
       bzero(&qp_init_attr, sizeof qp_init_attr); // 清零初始化结构体
       qp_init_attr.cap.max_recv_sge = MAX_SGE; // 最大接收离散内存块数
       qp_init_attr.cap.max_recv_wr = MAX_WR; // 接收队列深度
       qp_init_attr.cap.max_send_sge = MAX_SGE; // 最大发送离散内存块数
       qp_init_attr.cap.max_send_wr = MAX_WR; // 发送队列深度
       qp_init_attr.qp_type = IBV_QPT_RC; // 核心：选择“可靠连接” (Reliable Connection) 模式
       qp_init_attr.recv_cq = client_cq; // 完成时通知哪个 CQ
       qp_init_attr.send_cq = client_cq; // 发送完成也通知同一个 CQ
       
       /* 调用连接管理器创建关联到 cm_id 的 QP。 */
       ret = rdma_create_qp(cm_client_id,
		       pd,
		       &qp_init_attr);
	if (ret) {
		rdma_error("Failed to create QP, errno: %d \n", -errno);
	       return -errno;
	}
	client_qp = cm_client_id->qp; // 从 cm_id 中提取指向生成的 QP 的指针
	debug("QP created at %p \n", client_qp);
	return 0; // 初始化成功
}

/* 
 * 预发布接收缓冲区。
 * 在调用 rdma_connect() 之前，必须在接收队列挂载一个缓冲区，
 * 以便接收服务器发来的第一条握手元数据信息。
 */
static int client_pre_post_recv_buffer()
{
	int ret = -1;
	/* 1. 注册并准备用于存放服务器元数据的 MR。 */
	server_metadata_mr = rdma_buffer_register(pd,
			&server_metadata_attr, // 这是一个具体的本地结构体地址
			sizeof(server_metadata_attr),
			(IBV_ACCESS_LOCAL_WRITE)); // 允许网卡往里写
	if(!server_metadata_mr){
		rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
		return -ENOMEM;
	}
	
	/* 2. 填充 SGE (散集元素)，告知硬件具体的存放点。 */
	server_recv_sge.addr = (uint64_t) server_metadata_mr->addr; // 物理映射后的地址
	server_recv_sge.length = (uint32_t) server_metadata_mr->length; // 长度
	server_recv_sge.lkey = (uint32_t) server_metadata_mr->lkey; // 硬件凭证
	
	/* 3. 构造接收工作请求 (WR)。 */
	bzero(&server_recv_wr, sizeof(server_recv_wr));
	server_recv_wr.sg_list = &server_recv_sge; // 绑定的存放位置
	server_recv_wr.num_sge = 1; // 只有一个内存块
	
	/* 4. 发布请求。将其推入接收队列。 */
	ret = ibv_post_recv(client_qp,
		      &server_recv_wr,
		      &bad_server_recv_wr); // 如果发布失败，此指针指向出错的 WR
	if (ret) {
		rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
		return ret;
	}
	debug("Receive buffer pre-posting is successful \n");
	return 0;
}

/* 
 * 建立到 RDMA 服务器的物理连接。
 */
static int client_connect_to_server() 
{
	struct rdma_conn_param conn_param; // 连接参数
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;
	
	/* 1. 设置连接参数。 */
	bzero(&conn_param, sizeof(conn_param));
	conn_param.initiator_depth = 3; // 允许发起的 RDMA READ/ATOMIC 请求上限
	conn_param.responder_resources = 3; // 允许远程端对本端执行的并发请求上限
	conn_param.retry_count = 3; // 传输重试次数，连接超时会自动尝试 3 次
	
	/* 2. 发起连接。该操作会向服务器发送一个特殊的连接包。 */
	ret = rdma_connect(cm_client_id, &conn_param);
	if (ret) {
		rdma_error("Failed to connect to remote host , errno: %d\n", -errno);
		return -errno;
	}
	debug("waiting for cm event: RDMA_CM_EVENT_ESTABLISHED\n");
	
	/* 3. 等待连接完全建立。服务器接受连接后，我们会收到 ESTABLISHED 事件。 */
	ret = process_rdma_cm_event(cm_event_channel, 
			RDMA_CM_EVENT_ESTABLISHED,
			&cm_event);
	if (ret) {
		rdma_error("Failed to get cm event, ret = %d \n", ret);
	       return ret;
	}
	
	/* 4. 确认连接建立事件。 */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge cm event, errno: %d\n", 
			       -errno);
		return -errno;
	}
	printf("The client is connected successfully \n");
	return 0;
}

/* 
 * 与服务器交换内存元数据。
 * 客户端首先发送自己的内存区域信息，然后等待服务器发回其内存区域信息。
 * 只有交换了这些信息（地址和 RKey），双方才能执行单边 RDMA READ/WRITE。
 */
static int client_xchange_metadata_with_server()
{
	struct ibv_wc wc[2]; // 预期处理 2 个完成事件：发送成功和接收成功
	int ret = -1;
	
	/* 1. 注册用于存放源数据的缓冲区。 */
	client_src_mr = rdma_buffer_register(pd,
			src,
			strlen(src),
			(IBV_ACCESS_LOCAL_WRITE|
			 IBV_ACCESS_REMOTE_READ|
			 IBV_ACCESS_REMOTE_WRITE)); // 允许对端远程读和写
	if(!client_src_mr){
		rdma_error("Failed to register the first buffer, ret = %d \n", ret);
		return ret;
	}
	
	/* 2. 准备发往服务器的元数据属性。 */
	client_metadata_attr.address = (uint64_t) client_src_mr->addr; 
	client_metadata_attr.length = client_src_mr->length; 
	client_metadata_attr.stag.local_stag = client_src_mr->lkey;
	
	/* 3. 注册元数据本身所在的内存区域，网卡需要从这读取数据发送出去。 */
	client_metadata_mr = rdma_buffer_register(pd,
			&client_metadata_attr,
			sizeof(client_metadata_attr),
			IBV_ACCESS_LOCAL_WRITE);
	if(!client_metadata_mr) {
		rdma_error("Failed to register the client metadata buffer, ret = %d \n", ret);
		return ret;
	}
	
	/* 4. 配置 SGE。 */
	client_send_sge.addr = (uint64_t) client_metadata_mr->addr;
	client_send_sge.length = (uint32_t) client_metadata_mr->length;
	client_send_sge.lkey = client_metadata_mr->lkey;
	
	/* 5. 配置发送工作请求。这是一个标准的 RDMA SEND (双边操作)。 */
	bzero(&client_send_wr, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_SEND; // 核心：使用 SEND 操作码
	client_send_wr.send_flags = IBV_SEND_SIGNALED; // 请求完成后产生信号
	
	/* 6. 将请求发布到发送队列。 */
	ret = ibv_post_send(client_qp, 
		       &client_send_wr,
	       &bad_client_send_wr);
	if (ret) {
		rdma_error("Failed to send client metadata, errno: %d \n", 
				-errno);
		return -errno;
	}
	
	/* 7. 等待 2 个完成条目。
	 * 一个代表我们的 SEND 发出去了，一个代表我们之前 pre-post 的 RECV 收到了数据。
	 */
	ret = process_work_completion_events(io_completion_channel, 
			wc, 2);
	if(ret != 2) {
		rdma_error("We failed to get 2 work completions , ret = %d \n",
				ret);
		return ret;
	}
	debug("Server sent us its buffer location and credentials, showing \n");
	show_rdma_buffer_attr(&server_metadata_attr); // 此时 server_metadata_attr 已被网卡填充
	return 0;
}

/* 
 * 执行核心远程内存操作。
 * 演示：
 * 1) RDMA WRITE: 本地 src -> 远程服务器缓冲区
 * 2) RDMA READ : 远程服务器缓冲区 -> 本地 dst
 */ 
static int client_remote_memory_ops() 
{
	struct ibv_wc wc;
	int ret = -1;
	
	/* 1. 注册目标缓冲区。它是 RDMA READ 后存放数据的目的地。 */
	client_dst_mr = rdma_buffer_register(pd,
			dst,
			strlen(src),
			(IBV_ACCESS_LOCAL_WRITE | 
			 IBV_ACCESS_REMOTE_WRITE | 
			 IBV_ACCESS_REMOTE_READ));
	if (!client_dst_mr) {
		rdma_error("We failed to create the destination buffer, -ENOMEM\n");
		return -ENOMEM;
	}
	
	/* --- 步骤 1: RDMA WRITE 操作 --- */
	/* 配置源数据信息 (本地)。 */
	client_send_sge.addr = (uint64_t) client_src_mr->addr;
	client_send_sge.length = (uint32_t) client_src_mr->length;
	client_send_sge.lkey = client_src_mr->lkey;
	
	/* 配置发送工作请求。 */
	bzero(&client_send_wr, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_RDMA_WRITE; // 核心：切换为单边写操作
	client_send_wr.send_flags = IBV_SEND_SIGNALED;
	/* 必须告知网卡远程端的地址和密钥。 */
	client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag; // 服务器端的 key
	client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address; // 服务器端的起始地址
	
	/* 发布 WRITE 操作。服务器端不会对此有感知（除非它在轮询内存）。 */
	ret = ibv_post_send(client_qp, 
		       &client_send_wr,
	       &bad_client_send_wr);
	if (ret) {
		rdma_error("Failed to write client src buffer, errno: %d \n", 
				-errno);
		return -errno;
	}
	
	/* 等待完成。 */
	ret = process_work_completion_events(io_completion_channel, 
			&wc, 1);
	if(ret != 1) {
		rdma_error("We failed to get 1 work completions , ret = %d \n",
				ret);
		return ret;
	}
	debug("Client side WRITE is complete \n");
	
	/* --- 步骤 2: RDMA READ 操作 --- */
	/* 配置目的地信息 (本地)。读取到的数据将存放于此。 */
	client_send_sge.addr = (uint64_t) client_dst_mr->addr;
	client_send_sge.length = (uint32_t) client_dst_mr->length;
	client_send_sge.lkey = client_dst_mr->lkey;
	
	/* 配置请求。 */
	bzero(&client_send_wr, sizeof(client_send_wr));
	client_send_wr.sg_list = &client_send_sge;
	client_send_wr.num_sge = 1;
	client_send_wr.opcode = IBV_WR_RDMA_READ; // 核心：切换为单边读操作
	client_send_wr.send_flags = IBV_SEND_SIGNALED;
	/* 同样设置服务器端的信息。网卡将直接拉取这些数据并存入 client_dst_mr。 */
	client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
	client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;
	
	/* 发布 READ 操作。 */
	ret = ibv_post_send(client_qp, 
		       &client_send_wr,
	       &bad_client_send_wr);
	if (ret) {
		rdma_error("Failed to read client dst buffer from the master, errno: %d \n", 
				-errno);
		return -errno;
	}
	
	/* 等待 READ 传输完成。 */
	ret = process_work_completion_events(io_completion_channel, 
			&wc, 1);
	if(ret != 1) {
		rdma_error("We failed to get 1 work completions , ret = %d \n",
				ret);
		return ret;
	}
	debug("Client side READ is complete \n");
	return 0; // 读写演示成功完成
}

/* 
 * 正常断开连接并销毁所有资源。
 */
static int client_disconnect_and_clean()
{
	struct rdma_cm_event *cm_event = NULL;
	int ret = -1;
	
	/* 1. 主动发起断开请求。 */
	ret = rdma_disconnect(cm_client_id);
	if (ret) {
		rdma_error("Failed to disconnect, errno: %d \n", -errno);
		// 虽然失败，但通常会继续尝试清理其他资源
	}
	
	/* 2. 等待断开成功的确认事件。 */
	ret = process_rdma_cm_event(cm_event_channel, 
			RDMA_CM_EVENT_DISCONNECTED,
			&cm_event);
	if (ret) {
		rdma_error("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = %d\n",
				ret);
	}
	
	/* 3. 确认事件。 */
	ret = rdma_ack_cm_event(cm_event);
	if (ret) {
		rdma_error("Failed to acknowledge cm event, errno: %d\n", 
			       -errno);
	}
	
	/* 4. 销毁队列对。 */
	rdma_destroy_qp(cm_client_id);
	
	/* 5. 销毁连接标识符。 */
	ret = rdma_destroy_id(cm_client_id);
	if (ret) {
		rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
	}
	
	/* 6. 销毁完成队列。 */
	ret = ibv_destroy_cq(client_cq);
	if (ret) {
		rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
	}
	
	/* 7. 销毁 I/O 通知通道。 */
	ret = ibv_destroy_comp_channel(io_completion_channel);
	if (ret) {
		rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
	}
	
	/* 8. 注销所有内存区域。注销操作会告知硬件解绑对应的内存条目。 */
	rdma_buffer_deregister(server_metadata_mr);
	rdma_buffer_deregister(client_metadata_mr);	
	rdma_buffer_deregister(client_src_mr);	
	rdma_buffer_deregister(client_dst_mr);	
	
	/* 9. 释放 malloc 分配的系统内存。 */
	free(src);
	free(dst);
	
	/* 10. 销毁保护域。 */
	ret = ibv_dealloc_pd(pd);
	if (ret) {
		rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
	}
	
	/* 11. 最后销毁 CM 事件通道。 */
	rdma_destroy_event_channel(cm_event_channel);
	printf("Client resource clean up is complete \n");
	return 0;
}

/* 
 * 打印程序使用说明。
 */
void usage() {
	printf("Usage:\n");
	printf("rdma_client: [-a <server_addr>] [-p <server_port>] -s string (required)\n");
	printf("(default IP is 127.0.0.1 and port is %d)\n", DEFAULT_RDMA_PORT);
	exit(1);
}

/* 
 * 主函数：解析参数，按顺序执行 RDMA 握手和数据传输。
 */
int main(int argc, char **argv) {
	struct sockaddr_in server_sockaddr; // 服务器套接字地址结构
	int ret, option;
	
	bzero(&server_sockaddr, sizeof server_sockaddr); // 清零地址结构
	server_sockaddr.sin_family = AF_INET; // IPv4
	server_sockaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 默认本地回环 (127.0.0.1)
	
	src = dst = NULL; // 初始化缓冲区指针为空
	
	/* 命令行参数解析循环。 */
	while ((option = getopt(argc, argv, "s:a:p:")) != -1) {
		switch (option) {
			case 's': // 输入待传输的字符串
				printf("Passed string is : %s , with count %u \n", 
						optarg, 
						(unsigned int) strlen(optarg));
				src = calloc(strlen(optarg) , 1); // 为源数据分配内存
				if (!src) {
					rdma_error("Failed to allocate memory : -ENOMEM\n");
					return -ENOMEM;
				}
				/* 复制参数字符串。 */
				strncpy(src, optarg, strlen(optarg));
				dst = calloc(strlen(optarg), 1); // 为目的地分配等大内存
				if (!dst) {
					rdma_error("Failed to allocate destination memory, -ENOMEM\n");
					free(src);
					return -ENOMEM;
				}
				break;
			case 'a': // 解析 IP 地址
				ret = get_addr(optarg, (struct sockaddr*) &server_sockaddr);
				if (ret) {
					rdma_error("Invalid IP \n");
					return ret;
				}
				break;
			case 'p': // 解析端口号
				server_sockaddr.sin_port = htons(strtol(optarg, NULL, 0)); 
				break;
			default: // 打印帮助信息
				usage();
				break;
			}
		}
	// 检查端口。如果未指定，则使用默认值。
	if (!server_sockaddr.sin_port) {
	  server_sockaddr.sin_port = htons(DEFAULT_RDMA_PORT);
	  }
	// 必须提供待传输字符串。
	if (src == NULL) {
		printf("Please provide a string to copy \n");
		usage();
       	}
		
	/* --- 执行步骤 --- */
	
	/* 1. 准备连接相关的硬件和系统资源。 */
	ret = client_prepare_connection(&server_sockaddr);
	if (ret) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	 }
	 
	/* 2. 在连接建立前发布一个接收请求，用于后续握手。 */
	ret = client_pre_post_recv_buffer(); 
	if (ret) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	
	/* 3. 发起物理连接。 */
	ret = client_connect_to_server();
	if (ret) { 
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	
	/* 4. 与服务器交换内存凭证 (地址和 Key)。 */
	ret = client_xchange_metadata_with_server();
	if (ret) {
		rdma_error("Failed to setup client connection , ret = %d \n", ret);
		return ret;
	}
	
	/* 5. 执行具体的 RDMA WRITE 和 READ 数据操作。 */
	ret = client_remote_memory_ops();
	if (ret) {
		rdma_error("Failed to finish remote memory ops, ret = %d \n", ret);
		return ret;
	}
	
	/* 验证结果。 */
	if (check_src_dst()) {
		rdma_error("src and dst buffers do not match \n");
	} else {
		printf("...\nSUCCESS, source and destination buffers match \n");
	}
	
	/* 6. 断开连接并清理。 */
	ret = client_disconnect_and_clean();
	if (ret) {
		rdma_error("Failed to cleanly disconnect and clean up resources \n");
	}
	return ret; // 返回最终退出状态
}
