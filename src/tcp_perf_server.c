/*
 * Copyright (C) 2018 - 2019 Xilinx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

/** Connection handle for a TCP Server session */
#include "lwipopts.h"
#include "lwip/ip_addr.h"
#include "lwip/err.h"
#include "lwip/tcp.h"
#include "lwip/inet.h"
#include "xil_printf.h"
#include "platform.h"
#include <stdlib.h>

#define TCP_CONN_PORT 5001
#define MAX_FILENAME_LEN 64
#define TCP_SEND_BUFSIZE (7*1460)

static struct tcp_pcb *c_pcb;
static char data[314572800];
//static char data[209715200];
char* fp;
struct file {
	char		filename[MAX_FILENAME_LEN];
	uint32_t	length;
	uint32_t	recv_length;
	uint32_t	sent_length;
//	uint8_t 	data[];
};

uint8_t status;
// uint8_t api_send_flags = TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE;
uint8_t api_send_flags = TCP_WRITE_FLAG_MORE;
uint32_t remain;

struct file file_obj_ = { 0 };
const char* reset = "reset";
const char* sendback = "sendback";

void print_app_header(void)
{
	xil_printf("This is an simple file transfer server based on lwip.\n");
}

static void print_tcp_conn_stats(void)
{
	xil_printf("local %s port %d connected with ", inet_ntoa(c_pcb->local_ip), c_pcb->local_port);
	xil_printf("%s port %d\r\n",inet_ntoa(c_pcb->remote_ip), c_pcb->remote_port);
	xil_printf("status = %d\n\n", status);
}

/** Close a tcp session */
static void tcp_server_close(struct tcp_pcb *pcb)
{
	err_t err;
	struct file* file_obj = &file_obj_;

	fp = data;
	status = 0;
	memset(file_obj, 0, sizeof(struct file));

	if (pcb != NULL) {
		tcp_recv(pcb, NULL);
		tcp_err(pcb, NULL);
		err = tcp_close(pcb);
		if (err != ERR_OK) {
			/* Free memory with abort */
			tcp_abort(pcb);
		}
	}
}

/** Error callback, tcp session aborted */
static void tcp_server_err(void *arg, err_t err)
{
	LWIP_UNUSED_ARG(err);
	tcp_server_close(c_pcb);
	c_pcb = NULL;
	status = 0;
	xil_printf("TCP connection aborted\n\r");
}

static err_t tcp_sent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len) {
	err_t err;

	switch (status) {
	case 0:
		status = 1;
		xil_printf("FileName: %s\n", file_obj_.filename);
//		xil_printf("len: %u\n", len);
		break;
	case 1:
		status = 2;
		xil_printf("FileSize: %u\n", file_obj_.length);
//		xil_printf("len: %u\n", len);
		break;
	case 2:
		status = 3;
		xil_printf("Recv All Data\n");
//		xil_printf("len: %u\n", len);
		break;
	case 3:
		xil_printf("tcp_sent_callback case 3\n");
//		xil_printf("len: %u\n", len);
		break;
	case 4:
//		xil_printf("len: %u, sent: %u, remain: %u\n", len, file_obj_.sent_length, remain);
		if (file_obj_.sent_length == file_obj_.length) { // 发送完毕
			status = 3;
			xil_printf("Send All Data\n");
		} else if (tcp_sndbuf(c_pcb) > TCP_SEND_BUFSIZE) {
			if (remain > TCP_SEND_BUFSIZE) {
				err = tcp_write(c_pcb, fp + file_obj_.sent_length, TCP_SEND_BUFSIZE, api_send_flags);
				if (err == ERR_MEM) {
					xil_printf("41ERR_MEM\n");
					return err;
				}
				else if (err != ERR_OK) {
					xil_printf("41tcp_write(): Error on tcp_write: %d\r\n", err);
					return err;
				}
				file_obj_.sent_length += TCP_SEND_BUFSIZE;
				remain -= TCP_SEND_BUFSIZE;
			}
			else {
				err = tcp_write(c_pcb, fp + file_obj_.sent_length, remain, api_send_flags);
				if (err == ERR_MEM) {
					xil_printf("42ERR_MEM\n");
					return err;
				}
				else if (err != ERR_OK) {
					xil_printf("42tcp_write(): Error on tcp_write: %d\r\n", err);
					return err;
				}
				file_obj_.sent_length += remain;
				remain = 0;
			}
			err = tcp_output(c_pcb);
			if (err != ERR_OK) {
				xil_printf("43tcp_output(): Error on tcp_output: %d\r\n", err);
				return err;
			}
		}
		break;
	default:
		break;
	}

	return ERR_OK;
}

/** Receive data on a tcp session */
static err_t tcp_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	if (p == NULL) {
		tcp_server_close(tpcb);
		xil_printf("TCP test passed Successfully\n\r");
		return ERR_OK;
	}

	struct file* file_obj = &file_obj_;

	switch (status) {
	case 0: // 收到文件名，回复 filename 确认
		print_tcp_conn_stats();
		if(p->tot_len < MAX_FILENAME_LEN) {
			memcpy(file_obj->filename, p->payload, p->tot_len);
			tcp_write(c_pcb, "filename", strlen("filename"), api_send_flags);
			if (err != ERR_OK) {
				xil_printf("01tcp_write(): Error on tcp_write: %d\r\n", err);
				return err;
			}
			err = tcp_output(c_pcb);
			if (err != ERR_OK) {
				xil_printf("02tcp_output(): Error on tcp_output: %d\r\n", err);
				return err;
			}
		}
		tcp_recved(tpcb, p->tot_len);
		pbuf_free(p);
		break;
	case 1: // 收到文件大小，回复 filesize 确认
		print_tcp_conn_stats();
		// reset
		if (p->tot_len == strlen(reset) && strcmp((char*)p->payload, reset) == 0) {
			xil_printf("status 1 reset\n");
			status = 0;
			fp = data;
			memset(file_obj, 0, sizeof(struct file));
		}
		// filesize
		else {
			file_obj->length = strtoul((char*)p->payload, NULL, 10);
			tcp_write(c_pcb, "filesize", strlen("filesize"), api_send_flags);
			if (err != ERR_OK) {
				xil_printf("11tcp_write(): Error on tcp_write: %d\r\n", err);
				return err;
			}
			err = tcp_output(c_pcb);
			if (err != ERR_OK) {
				xil_printf("12tcp_output(): Error on tcp_output: %d\r\n", err);
				return err;
			}

			file_obj->recv_length = 0;
			fp = data;
		}
		tcp_recved(tpcb, p->tot_len);
		pbuf_free(p);
		break;
	case 2: // 接收文件数据
		// reset
		if (p->tot_len == strlen(reset) && strcmp((char*)p->payload, reset) == 0) {
			status = 0;
			fp = data;
			memset(file_obj, 0, sizeof(struct file));
		}
		// data
		else {
			memcpy(fp + file_obj->recv_length, p->payload, p->len);
			file_obj->recv_length += p->len;
			if (file_obj->recv_length == file_obj->length) {
				tcp_write(c_pcb, "recv all data", strlen("recv all data"), api_send_flags);
				if (err != ERR_OK) {
					xil_printf("21tcp_write(): Error on tcp_write: %d\r\n", err);
					return err;
				}
				err = tcp_output(c_pcb);
				if (err != ERR_OK) {
					xil_printf("22tcp_output(): Error on tcp_output: %d\r\n", err);
					return err;
				}
			}
		}
		tcp_recved(tpcb, p->tot_len);
		pbuf_free(p);
		break;
	case 3: // 返回文件数据
		// reset
		if (p->tot_len == strlen(reset) && strcmp((char*)p->payload, reset) == 0) {
			status = 0;
			fp = data;
			memset(file_obj, 0, sizeof(struct file));
		}
		// sendback
		else if (p->tot_len == strlen(sendback) && strcmp((char*)p->payload, sendback) == 0) {
			remain = file_obj->length;
			file_obj->sent_length = 0;
			status = 4;

			if (tcp_sndbuf(c_pcb) > TCP_SEND_BUFSIZE) {
				if (remain > TCP_SEND_BUFSIZE) {
					err = tcp_write(tpcb, fp + file_obj->sent_length, TCP_SEND_BUFSIZE, api_send_flags);
					if (err == ERR_MEM) {
						xil_printf("31ERR_MEM\n");
					}
					else if (err != ERR_OK) {
						xil_printf("31tcp_write(): Error on tcp_write: %d\r\n", err);
						return err;
					}
					file_obj_.sent_length += TCP_SEND_BUFSIZE;
					remain -= TCP_SEND_BUFSIZE;
				}
				else {
					err = tcp_write(c_pcb, fp + file_obj->sent_length, remain, api_send_flags);
					if (err == ERR_MEM) {
						xil_printf("32ERR_MEM\n");
					}
					else if (err != ERR_OK) {
						xil_printf("32tcp_write(): Error on tcp_write: %d\r\n", err);
						return err;
					}
					file_obj_.sent_length += remain;
					remain = 0;
				}
//				xil_printf("sent: %u, remain: %u\n", file_obj_.sent_length, remain);
				err = tcp_output(c_pcb);
				if (err != ERR_OK) {
					xil_printf("33tcp_output(): Error on tcp_output: %d\r\n", err);
					return err;
				}
			}
		}
		tcp_recved(tpcb, p->tot_len);
		pbuf_free(p);
		break;
	case 4:
		xil_printf("tcp_recv_callback case 4\n");
		tcp_recved(tpcb, p->tot_len);
		pbuf_free(p);
		break;
	default:
		xil_printf("Error : defualt\n");
		status = 0;
	}
	return ERR_OK;
}

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	if ((err != ERR_OK) || (newpcb == NULL)) {
		return ERR_VAL;
	}
	/* Save connected client PCB */
	c_pcb = newpcb;

	print_tcp_conn_stats();

	/* setup callbacks for tcp rx connection */
	tcp_arg(c_pcb, NULL);
	tcp_recv(c_pcb, tcp_recv_callback);
	tcp_sent(c_pcb, tcp_sent_callback);
	tcp_err(c_pcb, tcp_server_err);

	return ERR_OK;
}

void start_application(void)
{
	err_t err;
	struct tcp_pcb *pcb, *lpcb;

	/* Create Server PCB */
	pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
	if (!pcb) {
		xil_printf("TCP server: Error creating PCB. Out of Memory\r\n");
		return;
	}

	err = tcp_bind(pcb, IP_ADDR_ANY, TCP_CONN_PORT);
	if (err != ERR_OK) {
		xil_printf("TCP server: Unable to bind to port %d: err = %d\r\n", TCP_CONN_PORT, err);
		tcp_close(pcb);
		return;
	}

	/* Set connection queue limit to 1 to serve
	 * one client at a time
	 */
	lpcb = tcp_listen_with_backlog(pcb, 1);

	if (!lpcb) {
		xil_printf("TCP server: Out of memory while tcp_listen\r\n");
		tcp_close(pcb);
		return;
	}

	/* we do not need any arguments to callback functions */
	tcp_arg(lpcb, NULL);

	status = 0;
	fp = data;
	/* specify callback to use for incoming connections */
	tcp_accept(lpcb, tcp_server_accept);

	return;
}
