/*
 * 公共 RDMA 函数的实现。
 * 该文件包含了内存分配、注册、事件处理等底层操作。
 */

#include "rdma_common.h" // 包含公共头文件，获取函数原型和结构定义

/* 
 * 打印 RDMA 连接管理器标识符 (rdma_cm_id) 的详细信息。
 * cm_id 类似于 socket 编程中的文件描述符，但包含了更多的 RDMA 上下文信息。
 */
void show_rdma_cmid(struct rdma_cm_id *id)
{
	if(!id){ // 检查传入的指针是否为空
		rdma_error("Passed ptr is NULL\n"); // 打印错误信息
		return; // 返回
	}
	printf("RDMA cm id at %p \n", id); // 打印 cm_id 结构体的内存地址
	// 如果关联了 verbs 上下文（代表具体的 RDMA 设备）且设备存在
	if(id->verbs && id->verbs->device)
		printf("dev_ctx: %p (device name: %s) \n", id->verbs, 
				id->verbs->device->name); // 打印设备上下文地址和设备名称（如 mlx5_0）
	// 如果关联了事件通道，打印其地址
	if(id->channel)
		printf("cm event channel %p\n", id->channel);
	// 打印关联的队列对 (QP)、端口空间类型以及端口号
	printf("QP: %p, port_space %x, port_num %u \n", id->qp, 
			id->ps,
			id->port_num);
}

/* 
 * 打印内存缓冲区属性。这些属性将被发送到远程端，以便远程端进行直接内存访问。
 */
void show_rdma_buffer_attr(struct rdma_buffer_attr *attr){
	if(!attr){ // 检查指针是否为空
		rdma_error("Passed attr is NULL\n"); // 打印错误
		return; // 返回
	}
	printf("---------------------------------------------------------\n"); // 打印分隔线
	// 打印内存的起始虚拟地址、长度以及本地访问密钥 (lkey)
	printf("buffer attr, addr: %p , len: %u , stag : 0x%x \n", 
			(void*) attr->address, 
			(unsigned int) attr->length,
			attr->stag.local_stag);
	printf("---------------------------------------------------------\n"); // 打印分隔线
}

/* 
 * 分配系统内存并将其注册为内存区域 (Memory Region, MR)。
 * RDMA 操作只能在注册过的内存上进行。
 */
struct ibv_mr* rdma_buffer_alloc(struct ibv_pd *pd, uint32_t size,
    enum ibv_access_flags permission) 
{
	struct ibv_mr *mr = NULL; // 初始化 MR 指针为空
	if (!pd) { // 检查保护域 (PD) 是否为空
		rdma_error("Protection domain is NULL \n"); // PD 是资源隔离的核心，不能为空
		return NULL; // 返回空
	}
	// 1. 使用 calloc 分配系统内存，并确保内容清零
	void *buf = calloc(1, size);
	if (!buf) { // 检查内存分配是否成功
		rdma_error("failed to allocate buffer, -ENOMEM\n"); // 内存不足
		return NULL; // 返回空
	}
	debug("Buffer allocated: %p , len: %u \n", buf, size); // 打印调试信息，显示分配的内存地址
	// 2. 调用注册函数，将分配的系统内存映射到 RDMA 网卡
	mr = rdma_buffer_register(pd, buf, size, permission);
	if(!mr){ // 如果网卡端注册失败
		free(buf); // 释放刚才分配的系统内存，防止内存泄漏
	}
	return mr; // 返回成功创建的 MR
}

/* 
 * 将一段连续的虚拟内存地址注册到网卡硬件中。
 */
struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, 
		void *addr, uint32_t length, 
		enum ibv_access_flags permission)
{
	struct ibv_mr *mr = NULL; // 初始化 MR 指针
	if (!pd) { // 检查保护域是否存在
		rdma_error("Protection domain is NULL, ignoring \n");
		return NULL;
	}
	/* 
	 * ibv_reg_mr: 核心 Verbs API。
	 * 该函数会锁定物理内存页面（防止被系统置换到磁盘），并允许硬件直接访问。
	 * 参数包括保护域、起始地址、长度以及访问权限标志位。
	 */
	mr = ibv_reg_mr(pd, addr, length, permission);
	if (!mr) { // 检查注册是否成功
		rdma_error("Failed to create mr on buffer, errno: %d \n", -errno); // 如果失败，打印错误号
		return NULL; // 返回空
	}
	// 打印注册成功的详细信息，包括 lkey（用于本地数据传输的硬件凭证）
	debug("Registered: %p , len: %u , stag: 0x%x \n", 
			mr->addr, 
			(unsigned int) mr->length, 
			mr->lkey);
	return mr; // 返回 MR 指针
}

/* 
 * 释放 MR 及其对应的底层系统内存。
 */
void rdma_buffer_free(struct ibv_mr *mr) 
{
	if (!mr) { // 检查 MR 是否有效
		rdma_error("Passed memory region is NULL, ignoring\n");
		return ;
	}
	void *to_free = mr->addr; // 保存内存地址，以便后续释放系统内存
	// 1. 先注销 MR，告知网卡硬件这块内存不再可用
	rdma_buffer_deregister(mr);
	debug("Buffer %p free'ed\n", to_free); // 打印调试信息
	// 2. 释放真实的物理内存
	free(to_free);
}

/* 
 * 从网卡硬件中撤回已注册的内存条目。
 */
void rdma_buffer_deregister(struct ibv_mr *mr) 
{
	if (!mr) { // 检查 MR 是否为空
		rdma_error("Passed memory region is NULL, ignoring\n");
		return;
	}
	// 打印注销信息，包括地址和 key
	debug("Deregistered: %p , len: %u , stag : 0x%x \n", 
			mr->addr, 
			(unsigned int) mr->length, 
			mr->lkey);
	/* 
	 * ibv_dereg_mr: 核心 Verbs API。
	 * 释放硬件资源，并允许操作系统重新管理这些物理页面。
	 */
	ibv_dereg_mr(mr);
}

/* 
 * 等待并处理连接管理 (CM) 事件。
 * CM 事件处理连接的建立、地址解析等握手过程。
 */
int process_rdma_cm_event(struct rdma_event_channel *echannel, 
		enum rdma_cm_event_type expected_event,
		struct rdma_cm_event **cm_event)
{
	int ret = 1; // 初始化返回值为 1（通常表示出错）
	/* 
	 * rdma_get_cm_event: 阻塞式调用。
	 * 从通道中提取下一个发生的连接事件。
	 */
	ret = rdma_get_cm_event(echannel, cm_event);
	if (ret) { // 检查函数调用是否失败
		rdma_error("Failed to retrieve a cm event, errno: %d \n",
				-errno);
		return -errno; // 返回具体的错误码
	}
	/* 检查捕获到的事件内部状态 */
	if(0 != (*cm_event)->status){ // 如果状态码不为 0，说明事件本身指示了一个失败（如连接被拒）
		rdma_error("CM event has non zero status: %d\n", (*cm_event)->status);
		ret = -((*cm_event)->status); // 转换为负的错误码
		/* 
		 * 重要：必须调用 rdma_ack_cm_event 来确认事件。
		 * 否则会造成连接管理器的资源泄漏。
		 */
		rdma_ack_cm_event(*cm_event);
		return ret; // 返回错误
	}
	/* 检查事件类型是否符合调用者的期望 */
	if ((*cm_event)->event != expected_event) { // 如果收到了不匹配的事件
		rdma_error("Unexpected event received: %s [ expecting: %s ]", 
				rdma_event_str((*cm_event)->event),
				rdma_event_str(expected_event));
		/* 同样需要确认该事件 */
		rdma_ack_cm_event(*cm_event);
		return -1; // 返回不匹配错误
	}
	// 如果一切正常，打印捕获到的事件名称
	debug("A new %s type event is received \n", rdma_event_str((*cm_event)->event));
	/* 
	 * 注意：此处不调用 rdma_ack_cm_event。
	 * 调用者需要从 *cm_event 中读取数据（如新的 id），并在使用完毕后手动 Ack。
	 */
	return 0; // 成功
}

/* 
 * 处理完成队列 (CQ) 中的数据传输完成通知。
 * 这是数据路径 (Data Path) 上的核心函数。
 */
int process_work_completion_events (struct ibv_comp_channel *comp_channel, 
		struct ibv_wc *wc, int max_wc)
{
	struct ibv_cq *cq_ptr = NULL; // 用于接收产生通知的 CQ 指针
	void *context = NULL; // 用于接收用户定义的上下文
	int ret = -1, i, total_wc = 0; // 初始化变量

       /* 1. 等待 IO 完成通道上的通知。ibv_get_cq_event 会阻塞直到网卡写入一个新的完成条目。 */
	ret = ibv_get_cq_event(comp_channel, /* IO 监听通道 */ 
		       &cq_ptr, /* 返回是哪个完成队列产生了活动 */ 
		       &context); /* 返回 CQ 创建时绑定的用户上下文 */
       if (ret) { // 检查获取事件是否成功
	       rdma_error("Failed to get next CQ event due to %d \n", -errno);
	       return -errno;
       }

       /* 2. 申请下一次通知。ibv_req_notify_cq 告诉硬件在下一次传输完成时再次触发事件。通知是单次触发的。 */
       ret = ibv_req_notify_cq(cq_ptr, 0);
       if (ret){ // 检查申请通知是否成功
	       rdma_error("Failed to request further notifications %d \n", -errno);
	       return -errno;
       }

       /* 
        * 3. 轮询 (Polling) 完成队列。
        * 收到事件通知后，我们必须主动从队列中“收割”工作完成 (WC) 条目。
        */
       total_wc = 0; // 初始化收割总数
       do {
	       /* 
	        * ibv_poll_cq: 核心数据面 API。
	        * 从硬件队列中读取完成信息，这是一个非阻塞操作。
	        */
	       ret = ibv_poll_cq(cq_ptr /* 目标 CQ */, 
		       max_wc - total_wc /* 本次允许读取的最大数量 */,
		       wc + total_wc/* 将 WC 结构存储到数组的当前偏移位置 */);
	       if (ret < 0) { // 如果 poll 操作出错
		       rdma_error("Failed to poll cq for wc due to %d \n", ret);
		       return ret; // 返回错误码
	       }
	       total_wc += ret; // 累加成功读取的数量
       } while (total_wc < max_wc); // 循环直到拿到预期数量的消息（或队列为空）

       debug("%d WC are completed \n", total_wc); // 打印收到的 WC 总数

       /* 4. 检查每一个已完成操作的状态。 */
       for( i = 0 ; i < total_wc ; i++) {
	       if (wc[i].status != IBV_WC_SUCCESS) { // 如果状态不是 SUCCESS（如发生超时、地址错误等）
		       rdma_error("Work completion (WC) has error status: %s at index %d", 
				       ibv_wc_status_str(wc[i].status), i); // 打印错误状态描述
		       /* 返回负的状态值，调用者据此判断失败 */
		       return -(wc[i].status);
	       }
       }

       /* 5. 确认 CQ 事件。类似于 CM 事件，CQ 事件也需要 ACK 才能释放资源。 */
       ibv_ack_cq_events(cq_ptr, 
		       1 /* 我们确认处理了 1 个事件通知 */);
       return total_wc; // 返回成功收到的 WC 总数
}

/* 
 * 解析主机名或 IP 地址。
 */
int get_addr(char *dst, struct sockaddr *addr)
{
	struct addrinfo *res; // 存储解析结果
	int ret = -1; // 返回值
	// 调用标准库函数解析地址信息
	ret = getaddrinfo(dst, NULL, NULL, &res);
	if (ret) { // 检查解析是否出错
		rdma_error("getaddrinfo failed - invalid hostname or IP address\n");
		return ret;
	}
	// 将解析到的第一个地址复制到目标结构体中（假设为 IPv4）
	memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
	freeaddrinfo(res); // 释放解析结果占用的内存
	return ret; // 返回结果
}
