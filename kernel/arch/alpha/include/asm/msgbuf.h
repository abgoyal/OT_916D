
#ifndef _ALPHA_MSGBUF_H
#define _ALPHA_MSGBUF_H


struct msqid64_ds {
	struct ipc64_perm msg_perm;
	__kernel_time_t msg_stime;	/* last msgsnd time */
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	__kernel_time_t msg_ctime;	/* last change time */
	unsigned long  msg_cbytes;	/* current number of bytes on queue */
	unsigned long  msg_qnum;	/* number of messages in queue */
	unsigned long  msg_qbytes;	/* max number of bytes on queue */
	__kernel_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_pid_t msg_lrpid;	/* last receive pid */
	unsigned long  __unused1;
	unsigned long  __unused2;
};

#endif /* _ALPHA_MSGBUF_H */
