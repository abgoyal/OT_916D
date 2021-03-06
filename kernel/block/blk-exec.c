
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>

#include "blk.h"

#include <scsi/scsi_cmnd.h>

static void blk_end_sync_rq(struct request *rq, int error)
{
	struct completion *waiting = rq->end_io_data;

	rq->end_io_data = NULL;
	__blk_put_request(rq->q, rq);

	/*
	 * complete last, if this is a stack request the process (and thus
	 * the rq pointer) could be invalid right after this complete()
	 */
	complete(waiting);
}

void blk_execute_rq_nowait(struct request_queue *q, struct gendisk *bd_disk,
			   struct request *rq, int at_head,
			   rq_end_io_fn *done)
{
	int where = at_head ? ELEVATOR_INSERT_FRONT : ELEVATOR_INSERT_BACK;

	rq->rq_disk = bd_disk;
	rq->end_io = done;
	WARN_ON(irqs_disabled());
	spin_lock_irq(q->queue_lock);
	__elv_add_request(q, rq, where, 1);
	__generic_unplug_device(q);
	/* the queue is stopped so it won't be plugged+unplugged */
	if (blk_pm_resume_request(rq))
		q->request_fn(q);
	spin_unlock_irq(q->queue_lock);
}
EXPORT_SYMBOL_GPL(blk_execute_rq_nowait);

int blk_execute_rq(struct request_queue *q, struct gendisk *bd_disk,
		   struct request *rq, int at_head)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	char sense[SCSI_SENSE_BUFFERSIZE];
	int err = 0;

	/*
	 * we need an extra reference to the request, so we can look at
	 * it after io completion
	 */
	rq->ref_count++;

	if (!rq->sense) {
		memset(sense, 0, sizeof(sense));
		rq->sense = sense;
		rq->sense_len = 0;
	}

	rq->end_io_data = &wait;
	blk_execute_rq_nowait(q, bd_disk, rq, at_head, blk_end_sync_rq);
	wait_for_completion(&wait);

	if (rq->errors)
		err = -EIO;

	return err;
}
EXPORT_SYMBOL(blk_execute_rq);
