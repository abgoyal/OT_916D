
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_quota.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_attr.h"
#include "xfs_buf_item.h"
#include "xfs_trans_priv.h"
#include "xfs_qm.h"

/* ARGSUSED */
STATIC uint
xfs_qm_dquot_logitem_size(
	xfs_dq_logitem_t	*logitem)
{
	/*
	 * we need only two iovecs, one for the format, one for the real thing
	 */
	return (2);
}

STATIC void
xfs_qm_dquot_logitem_format(
	xfs_dq_logitem_t	*logitem,
	xfs_log_iovec_t		*logvec)
{
	ASSERT(logitem);
	ASSERT(logitem->qli_dquot);

	logvec->i_addr = (xfs_caddr_t)&logitem->qli_format;
	logvec->i_len  = sizeof(xfs_dq_logformat_t);
	logvec->i_type = XLOG_REG_TYPE_QFORMAT;
	logvec++;
	logvec->i_addr = (xfs_caddr_t)&logitem->qli_dquot->q_core;
	logvec->i_len  = sizeof(xfs_disk_dquot_t);
	logvec->i_type = XLOG_REG_TYPE_DQUOT;

	ASSERT(2 == logitem->qli_item.li_desc->lid_size);
	logitem->qli_format.qlf_size = 2;

}

STATIC void
xfs_qm_dquot_logitem_pin(
	xfs_dq_logitem_t *logitem)
{
	xfs_dquot_t *dqp = logitem->qli_dquot;

	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	atomic_inc(&dqp->q_pincount);
}

/* ARGSUSED */
STATIC void
xfs_qm_dquot_logitem_unpin(
	xfs_dq_logitem_t *logitem)
{
	xfs_dquot_t *dqp = logitem->qli_dquot;

	ASSERT(atomic_read(&dqp->q_pincount) > 0);
	if (atomic_dec_and_test(&dqp->q_pincount))
		wake_up(&dqp->q_pinwait);
}

/* ARGSUSED */
STATIC void
xfs_qm_dquot_logitem_unpin_remove(
	xfs_dq_logitem_t *logitem,
	xfs_trans_t	 *tp)
{
	xfs_qm_dquot_logitem_unpin(logitem);
}

STATIC void
xfs_qm_dquot_logitem_push(
	xfs_dq_logitem_t	*logitem)
{
	xfs_dquot_t	*dqp;
	int		error;

	dqp = logitem->qli_dquot;

	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	ASSERT(!completion_done(&dqp->q_flush));

	/*
	 * Since we were able to lock the dquot's flush lock and
	 * we found it on the AIL, the dquot must be dirty.  This
	 * is because the dquot is removed from the AIL while still
	 * holding the flush lock in xfs_dqflush_done().  Thus, if
	 * we found it in the AIL and were able to obtain the flush
	 * lock without sleeping, then there must not have been
	 * anyone in the process of flushing the dquot.
	 */
	error = xfs_qm_dqflush(dqp, 0);
	if (error)
		xfs_fs_cmn_err(CE_WARN, dqp->q_mount,
			"xfs_qm_dquot_logitem_push: push error %d on dqp %p",
			error, dqp);
	xfs_dqunlock(dqp);
}

/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_qm_dquot_logitem_committed(
	xfs_dq_logitem_t	*l,
	xfs_lsn_t		lsn)
{
	/*
	 * We always re-log the entire dquot when it becomes dirty,
	 * so, the latest copy _is_ the only one that matters.
	 */
	return (lsn);
}


void
xfs_qm_dqunpin_wait(
	xfs_dquot_t	*dqp)
{
	ASSERT(XFS_DQ_IS_LOCKED(dqp));
	if (atomic_read(&dqp->q_pincount) == 0)
		return;

	/*
	 * Give the log a push so we don't wait here too long.
	 */
	xfs_log_force(dqp->q_mount, 0);
	wait_event(dqp->q_pinwait, (atomic_read(&dqp->q_pincount) == 0));
}

STATIC void
xfs_qm_dquot_logitem_pushbuf(
	xfs_dq_logitem_t    *qip)
{
	xfs_dquot_t	*dqp;
	xfs_mount_t	*mp;
	xfs_buf_t	*bp;

	dqp = qip->qli_dquot;
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	/*
	 * If flushlock isn't locked anymore, chances are that the
	 * inode flush completed and the inode was taken off the AIL.
	 * So, just get out.
	 */
	if (completion_done(&dqp->q_flush)  ||
	    ((qip->qli_item.li_flags & XFS_LI_IN_AIL) == 0)) {
		xfs_dqunlock(dqp);
		return;
	}
	mp = dqp->q_mount;
	bp = xfs_incore(mp->m_ddev_targp, qip->qli_format.qlf_blkno,
			mp->m_quotainfo->qi_dqchunklen, XBF_TRYLOCK);
	xfs_dqunlock(dqp);
	if (!bp)
		return;
	if (XFS_BUF_ISDELAYWRITE(bp))
		xfs_buf_delwri_promote(bp);
	xfs_buf_relse(bp);
	return;

}

STATIC uint
xfs_qm_dquot_logitem_trylock(
	xfs_dq_logitem_t	*qip)
{
	xfs_dquot_t		*dqp;

	dqp = qip->qli_dquot;
	if (atomic_read(&dqp->q_pincount) > 0)
		return XFS_ITEM_PINNED;

	if (! xfs_qm_dqlock_nowait(dqp))
		return XFS_ITEM_LOCKED;

	if (!xfs_dqflock_nowait(dqp)) {
		/*
		 * dquot has already been flushed to the backing buffer,
		 * leave it locked, pushbuf routine will unlock it.
		 */
		return XFS_ITEM_PUSHBUF;
	}

	ASSERT(qip->qli_item.li_flags & XFS_LI_IN_AIL);
	return XFS_ITEM_SUCCESS;
}


STATIC void
xfs_qm_dquot_logitem_unlock(
	xfs_dq_logitem_t    *ql)
{
	xfs_dquot_t	*dqp;

	ASSERT(ql != NULL);
	dqp = ql->qli_dquot;
	ASSERT(XFS_DQ_IS_LOCKED(dqp));

	/*
	 * Clear the transaction pointer in the dquot
	 */
	dqp->q_transp = NULL;

	/*
	 * dquots are never 'held' from getting unlocked at the end of
	 * a transaction.  Their locking and unlocking is hidden inside the
	 * transaction layer, within trans_commit. Hence, no LI_HOLD flag
	 * for the logitem.
	 */
	xfs_dqunlock(dqp);
}


/* ARGSUSED */
STATIC void
xfs_qm_dquot_logitem_committing(
	xfs_dq_logitem_t	*l,
	xfs_lsn_t		lsn)
{
	return;
}


static struct xfs_item_ops xfs_dquot_item_ops = {
	.iop_size	= (uint(*)(xfs_log_item_t*))xfs_qm_dquot_logitem_size,
	.iop_format	= (void(*)(xfs_log_item_t*, xfs_log_iovec_t*))
					xfs_qm_dquot_logitem_format,
	.iop_pin	= (void(*)(xfs_log_item_t*))xfs_qm_dquot_logitem_pin,
	.iop_unpin	= (void(*)(xfs_log_item_t*))xfs_qm_dquot_logitem_unpin,
	.iop_unpin_remove = (void(*)(xfs_log_item_t*, xfs_trans_t*))
					xfs_qm_dquot_logitem_unpin_remove,
	.iop_trylock	= (uint(*)(xfs_log_item_t*))
					xfs_qm_dquot_logitem_trylock,
	.iop_unlock	= (void(*)(xfs_log_item_t*))xfs_qm_dquot_logitem_unlock,
	.iop_committed	= (xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_qm_dquot_logitem_committed,
	.iop_push	= (void(*)(xfs_log_item_t*))xfs_qm_dquot_logitem_push,
	.iop_pushbuf	= (void(*)(xfs_log_item_t*))
					xfs_qm_dquot_logitem_pushbuf,
	.iop_committing = (void(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_qm_dquot_logitem_committing
};

void
xfs_qm_dquot_logitem_init(
	struct xfs_dquot *dqp)
{
	xfs_dq_logitem_t  *lp;
	lp = &dqp->q_logitem;

	xfs_log_item_init(dqp->q_mount, &lp->qli_item, XFS_LI_DQUOT,
					&xfs_dquot_item_ops);
	lp->qli_dquot = dqp;
	lp->qli_format.qlf_type = XFS_LI_DQUOT;
	lp->qli_format.qlf_id = be32_to_cpu(dqp->q_core.d_id);
	lp->qli_format.qlf_blkno = dqp->q_blkno;
	lp->qli_format.qlf_len = 1;
	/*
	 * This is just the offset of this dquot within its buffer
	 * (which is currently 1 FSB and probably won't change).
	 * Hence 32 bits for this offset should be just fine.
	 * Alternatively, we can store (bufoffset / sizeof(xfs_dqblk_t))
	 * here, and recompute it at recovery time.
	 */
	lp->qli_format.qlf_boffset = (__uint32_t)dqp->q_bufoffset;
}

/*------------------  QUOTAOFF LOG ITEMS  -------------------*/

/*ARGSUSED*/
STATIC uint
xfs_qm_qoff_logitem_size(xfs_qoff_logitem_t *qf)
{
	return (1);
}

STATIC void
xfs_qm_qoff_logitem_format(xfs_qoff_logitem_t	*qf,
			   xfs_log_iovec_t	*log_vector)
{
	ASSERT(qf->qql_format.qf_type == XFS_LI_QUOTAOFF);

	log_vector->i_addr = (xfs_caddr_t)&(qf->qql_format);
	log_vector->i_len = sizeof(xfs_qoff_logitem_t);
	log_vector->i_type = XLOG_REG_TYPE_QUOTAOFF;
	qf->qql_format.qf_size = 1;
}


/*ARGSUSED*/
STATIC void
xfs_qm_qoff_logitem_pin(xfs_qoff_logitem_t *qf)
{
	return;
}


/*ARGSUSED*/
STATIC void
xfs_qm_qoff_logitem_unpin(xfs_qoff_logitem_t *qf)
{
	return;
}

/*ARGSUSED*/
STATIC void
xfs_qm_qoff_logitem_unpin_remove(xfs_qoff_logitem_t *qf, xfs_trans_t *tp)
{
	return;
}

/*ARGSUSED*/
STATIC uint
xfs_qm_qoff_logitem_trylock(xfs_qoff_logitem_t *qf)
{
	return XFS_ITEM_LOCKED;
}

/*ARGSUSED*/
STATIC void
xfs_qm_qoff_logitem_unlock(xfs_qoff_logitem_t *qf)
{
	return;
}

/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_qm_qoff_logitem_committed(xfs_qoff_logitem_t *qf, xfs_lsn_t lsn)
{
	return (lsn);
}

/*ARGSUSED*/
STATIC void
xfs_qm_qoff_logitem_push(xfs_qoff_logitem_t *qf)
{
	return;
}


/*ARGSUSED*/
STATIC xfs_lsn_t
xfs_qm_qoffend_logitem_committed(
	xfs_qoff_logitem_t *qfe,
	xfs_lsn_t lsn)
{
	xfs_qoff_logitem_t	*qfs;
	struct xfs_ail		*ailp;

	qfs = qfe->qql_start_lip;
	ailp = qfs->qql_item.li_ailp;
	spin_lock(&ailp->xa_lock);
	/*
	 * Delete the qoff-start logitem from the AIL.
	 * xfs_trans_ail_delete() drops the AIL lock.
	 */
	xfs_trans_ail_delete(ailp, (xfs_log_item_t *)qfs);
	kmem_free(qfs);
	kmem_free(qfe);
	return (xfs_lsn_t)-1;
}

/* ARGSUSED */
STATIC void
xfs_qm_qoff_logitem_committing(xfs_qoff_logitem_t *qip, xfs_lsn_t commit_lsn)
{
	return;
}

/* ARGSUSED */
STATIC void
xfs_qm_qoffend_logitem_committing(xfs_qoff_logitem_t *qip, xfs_lsn_t commit_lsn)
{
	return;
}

static struct xfs_item_ops xfs_qm_qoffend_logitem_ops = {
	.iop_size	= (uint(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_size,
	.iop_format	= (void(*)(xfs_log_item_t*, xfs_log_iovec_t*))
					xfs_qm_qoff_logitem_format,
	.iop_pin	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_pin,
	.iop_unpin	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_unpin,
	.iop_unpin_remove = (void(*)(xfs_log_item_t*,xfs_trans_t*))
					xfs_qm_qoff_logitem_unpin_remove,
	.iop_trylock	= (uint(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_trylock,
	.iop_unlock	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_unlock,
	.iop_committed	= (xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_qm_qoffend_logitem_committed,
	.iop_push	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_push,
	.iop_pushbuf	= NULL,
	.iop_committing = (void(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_qm_qoffend_logitem_committing
};

static struct xfs_item_ops xfs_qm_qoff_logitem_ops = {
	.iop_size	= (uint(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_size,
	.iop_format	= (void(*)(xfs_log_item_t*, xfs_log_iovec_t*))
					xfs_qm_qoff_logitem_format,
	.iop_pin	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_pin,
	.iop_unpin	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_unpin,
	.iop_unpin_remove = (void(*)(xfs_log_item_t*,xfs_trans_t*))
					xfs_qm_qoff_logitem_unpin_remove,
	.iop_trylock	= (uint(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_trylock,
	.iop_unlock	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_unlock,
	.iop_committed	= (xfs_lsn_t(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_qm_qoff_logitem_committed,
	.iop_push	= (void(*)(xfs_log_item_t*))xfs_qm_qoff_logitem_push,
	.iop_pushbuf	= NULL,
	.iop_committing = (void(*)(xfs_log_item_t*, xfs_lsn_t))
					xfs_qm_qoff_logitem_committing
};

xfs_qoff_logitem_t *
xfs_qm_qoff_logitem_init(
	struct xfs_mount *mp,
	xfs_qoff_logitem_t *start,
	uint flags)
{
	xfs_qoff_logitem_t	*qf;

	qf = (xfs_qoff_logitem_t*) kmem_zalloc(sizeof(xfs_qoff_logitem_t), KM_SLEEP);

	xfs_log_item_init(mp, &qf->qql_item, XFS_LI_QUOTAOFF, start ?
			&xfs_qm_qoffend_logitem_ops : &xfs_qm_qoff_logitem_ops);
	qf->qql_item.li_mountp = mp;
	qf->qql_format.qf_type = XFS_LI_QUOTAOFF;
	qf->qql_format.qf_flags = flags;
	qf->qql_start_lip = start;
	return (qf);
}
