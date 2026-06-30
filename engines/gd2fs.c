/*
 * libgd2fs engine
 *
 * this engine read/write gd2fs file with libgd2fs.
 *
 * Copyright (c) 2025 Beijing Tensorfer Co., Ltd. All rights reserved.
 *
 */

#include "../fio.h"
#include "../optgroup.h"

#include <stdlib.h>
#include <poll.h>
#include <xfer/gd2fs.h>

#define GD2FS_TIMEOUT_MS 3000

struct gd2fs_options {
	void *pad; /* .off1 = 0 is not allowed */
	char *cpaddr;
	char *dpaddr;
	char *cluster;
	unsigned int substreams;
	unsigned int io_threads;
	unsigned int mem_threads;
	unsigned int max_sge_size;
	char *log_level;
};

struct gd2fs_event {
	struct flist_head entry;
	struct io_u *io_u;
};

struct gd2fs_data {
	xfer_gd2fs_ctx *xctx;
	xfer_gd2fs_iomem *iomem;
	struct flist_head events; /* completed event list head of struct gd2fs_event */
	size_t queued;
	size_t completed;
};

static struct fio_option options[] = {
	{
		.name = "gd2fs-cpaddr",
		.lname = "gd2fs-cpaddr",
		.type = FIO_OPT_STR_STORE,
		.off1 = offsetof(struct gd2fs_options, cpaddr),
		.help = "gd2fs ctrl plane addresses. Ex gd2fs://127.0.0.1:7527, or gd2fs://127.0.0.1:7527,gd2fs://127.0.0.1:7528",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = "gd2fs-dpaddr",
		.lname = "gd2fs-dpaddr",
		.type = FIO_OPT_STR_STORE,
		.off1 = offsetof(struct gd2fs_options, dpaddr),
		.help = "gd2fs data plane addresses. Ex tcp://192.168.1.100, or tcp://192.168.1.100,tcp://192.168.1.101",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = "gd2fs-cluster",
		.lname = "gd2fs-cluster",
		.type = FIO_OPT_STR_STORE,
		.off1 = offsetof(struct gd2fs_options, cluster),
		.help = "gd2fs cluster ID",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = "gd2fs-substreams",
		.lname = "gd2fs-substreams",
		.type = FIO_OPT_INT,
		.off1 = offsetof(struct gd2fs_options, substreams),
		.help = "The number of substreams per connection",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = "gd2fs-io-threads",
		.lname = "gd2fs-io-threads",
		.type = FIO_OPT_INT,
		.off1 = offsetof(struct gd2fs_options, io_threads),
		.help = "The number of threads to accelerate network performance",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = "gd2fs-mem-threads",
		.lname = "gd2fs-mem-threads",
		.type = FIO_OPT_INT,
		.off1 = offsetof(struct gd2fs_options, mem_threads),
		.help = "The number of threads to accelerate memory performance",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = "gd2fs-max-sge-size",
		.lname = "gd2fs-max-sge-size",
		.type = FIO_OPT_INT,
		.off1 = offsetof(struct gd2fs_options, max_sge_size),
		.help = "The size of a single SGE in bytes",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = "gd2fs-log-level",
		.lname = "gd2fs-log-level",
		.type = FIO_OPT_STR_STORE,
		.off1 = offsetof(struct gd2fs_options, log_level),
		.help = "gd2fs log level: info, warn or error",
		.category = FIO_OPT_C_ENGINE,
		.group = FIO_OPT_G_GD2FS,
	},
	{
		.name = NULL,
	},
};

static int fio_gd2fs_setup(struct thread_data *td)
{
	return 0;
}

static int fio_gd2fs_init(struct thread_data *td)
{
	struct gd2fs_options *xoptions = td->eo;
	struct gd2fs_data *xdata = calloc(1, sizeof(*xdata));
	xfer_gd2fs_options xopts = { 0 };

	if (!xoptions->cpaddr) {
		log_err("gd2fs: missing required *cpaddr*\n");
		return 1;
	}

	if (!xoptions->dpaddr) {
		log_err("gd2fs: missing required *dpaddr*\n");
		return 1;
	}

	if (!xoptions->cluster) {
		log_err("gd2fs: missing required *cluster*\n");
		return 1;
	}

	if (!xoptions->max_sge_size) {
		xoptions->max_sge_size = 2 * 1024 * 1024 * 1024UL;
	}

	xopts.cp_address = xoptions->cpaddr;
	xopts.dp_address = xoptions->dpaddr;
	xopts.cluster_id = xoptions->cluster;
	xopts.iothread_count = xoptions->io_threads;
	xopts.memthread_count = xoptions->mem_threads ? xoptions->mem_threads : 1;
	xopts.conn_stream_count = xoptions->substreams ? xoptions->substreams : 1;
	xopts.log_level = xoptions->log_level;
	xopts.timeout = 3000;

	xdata->xctx = xfer_gd2fs_connect(&xopts);
	if (!xdata->xctx) {
		log_err("gd2fs :Failed to connect gd2fs: %s\n", xoptions->cpaddr);
		return 1;
	}

	INIT_FLIST_HEAD(&xdata->events);
	td->io_ops_data = xdata;

	return 0;
}

static int fio_gd2fs_post_init(struct thread_data *td)
{
	struct gd2fs_data *xdata = td->io_ops_data;
	xfer_gd2fs_ctx *xctx = xdata->xctx;

	/* td->orig_buffer and td->orig_buffer_size are ready in this stage */
	xdata->iomem = xfer_gd2fs_reg_iomem(xctx, td->orig_buffer, td->orig_buffer_size, NULL);
	if (!xdata->iomem) {
		log_err("gd2fs: Failed to reg io mem\n");
		return 1;
	}

	return 0;
}

static int fio_gd2fs_open_file(struct thread_data *td, struct fio_file *f)
{
	struct gd2fs_options *xoptions = td->eo;
	struct gd2fs_data *xdata = td->io_ops_data;
	xfer_gd2fs_ctx *xctx = xdata->xctx;
	xfer_gd2fs_stat sb = {0};
	xfer_gd2fs_completion completion = {0};
	void *id = (void *)__func__;
	uint8_t err_class, err_code;

	if (f->real_file_size != -1ULL) {
		return 0;
	}

	xfer_gd2fs_fstat(xctx, id, f->file_name, &sb);
	xfer_gd2fs_wait(xctx, &completion, 1, GD2FS_TIMEOUT_MS);

	assert(completion.id == id);
	err_code = XFER_GD2FS_STATUS_CODE(completion.status);
	if (!err_code) {
		f->real_file_size = sb.size;
		return 0;
	}

	if (!td_write(td)) {
		log_err("gd2fs: Failed to stat %s%s for READ: %d(%s)", xoptions->cpaddr, f->file_name, err_code, strerror(err_code));
		return -err_code;
	}

	id = (void *)__func__;
	xfer_gd2fs_create(xctx, id, f->file_name, 0, f->io_size);
	xfer_gd2fs_wait(xctx, &completion, 1, GD2FS_TIMEOUT_MS);
	err_class = XFER_GD2FS_STATUS_CLASS(completion.status);
	err_code = XFER_GD2FS_STATUS_CODE(completion.status);
	if (err_code) {
		/* multiple jobs create the same file, only one succeeds, others get EEXIST from server */
		if ((err_class != XFER_GD2FS_STATUS_CLASS_SERVER) || (err_code != EEXIST)) {
			log_err("gd2fs: Failed to create %s%s for WRITE %d(%s)", xoptions->cpaddr, f->file_name, err_code, strerror(err_code));
			return -err_code;
		}
	}

	f->real_file_size = f->io_size;
	return 0;
}

static inline bool __fio_gd2fs_io_buf_valid(struct thread_data *td, struct io_u *io_u)
{
	unsigned char *start = (unsigned char *)td->orig_buffer;
	unsigned char *end = start + td->orig_buffer_size;
	unsigned char *io_start = io_u->buf;
	unsigned char *io_end = io_start + io_u->buflen;

	if (io_start < start || io_end > end) {
		log_err("gd2fs: IO mem [%p - %p], IO buf [%p - %p]", start, end, io_start, io_end);
		return false;
	}

	return true;
}

static enum fio_q_status fio_gd2fs_queue(struct thread_data *td, struct io_u *io_u)
{
	struct gd2fs_options *xoptions = td->eo;
	unsigned int max_sge_size = xoptions->max_sge_size, nsge = 0;
	struct gd2fs_data *xdata = td->io_ops_data;
	xfer_gd2fs_ctx *xctx = xdata->xctx;
	enum fio_q_status ret = FIO_Q_QUEUED;
	ssize_t bytes = 0;
	xfer_gd2fs_sge *sges, *sge;
	unsigned char *buf = io_u->buf;

	if (!__fio_gd2fs_io_buf_valid(td, io_u)) {
		td->error = 1;
		return FIO_Q_COMPLETED;
	}

	nsge = (io_u->buflen + max_sge_size - 1) / max_sge_size;
	sges = calloc(sizeof(xfer_gd2fs_sge), nsge);

	for (unsigned int i = 0; i < nsge; i++) {
		sge = &sges[i];

		sge->len = min(io_u->buflen - bytes, (unsigned long long)max_sge_size);
		sge->addr = buf + bytes;
		sge->iomem = xdata->iomem;

		bytes += sge->len;
	}

	switch (io_u->ddir) {
	case DDIR_WRITE:
		/* TODO direct IO support */
		bytes = xfer_gd2fs_pwritev(xctx, io_u, io_u->file->file_name, io_u->offset, sges, nsge, 0);
		break;

	case DDIR_READ:
		bytes = xfer_gd2fs_preadv(xctx, io_u, io_u->file->file_name, io_u->offset, sges, nsge, 0);
		break;

	default:
		log_err("gd2fs: unsupported opcode %d", io_u->ddir);
		td->error = 1;
		ret = FIO_Q_COMPLETED;
	}

	if (bytes < 0) {
		log_err("gd2fs: failed to issue RW request: %s(%ld)\n", strerror(-bytes), -bytes);
		td->error = 1;
		ret = FIO_Q_COMPLETED;
	} else {
		xdata->queued++;
	}

	free(sges);
	return ret;
}

static int fio_gd2fs_getevents(struct thread_data *td, unsigned int min,
                                unsigned int max, const struct timespec *t)
{
	static xfer_gd2fs_completion *completions;
	static int ncompletion = 0;

	int timeout_ms = t ? t->tv_sec * 1000 + t->tv_nsec / 1000000 : -1;
	struct gd2fs_data *xdata = td->io_ops_data;
	xfer_gd2fs_ctx *xctx = xdata->xctx;
	xfer_gd2fs_completion *completion;
	struct gd2fs_event *xevent;
	uint8_t err_code;
	int ret;

	if (max > ncompletion) {
		completions = realloc(completions, sizeof(xfer_gd2fs_completion) * max);
		ncompletion = max;
	}

	ret = xfer_gd2fs_wait(xctx, completions, max, timeout_ms);
	if (ret > 0) {
		xdata->completed += ret;
		for (int i = 0; i < ret; i++) {
			completion = &completions[i];
			err_code = XFER_GD2FS_STATUS_CODE(completion->status);
			if (err_code) {
				log_err("gd2fs: IO error: %d(%s)\n", err_code, strerror(err_code));
				td->error = 1;
				return -1;
			}

			xevent = malloc(sizeof(struct gd2fs_event));
			xevent->io_u = completion->id;
			flist_add_tail(&xevent->entry, &xdata->events);
		}
	}

	return ret;
}

static struct io_u *fio_gd2fs_event(struct thread_data *td, int event)
{
	struct gd2fs_data *xdata = td->io_ops_data;
	struct gd2fs_event *xevent;
	struct io_u *io_u;

	if (flist_empty(&xdata->events)) {
		log_err("gd2fs: unexpected empty events list\n");
		td->error = 1;
		return NULL;
	}

	xevent = flist_first_entry(&xdata->events, struct gd2fs_event, entry);
	flist_del(&xevent->entry);

	io_u = xevent->io_u;
	free(xevent);

	return io_u;
}

static void fio_gd2fs_cleanup(struct thread_data *td)
{
	struct gd2fs_data *xdata = td->io_ops_data;
	xfer_gd2fs_ctx *xctx = xdata->xctx;
	size_t towait;
	xfer_gd2fs_completion *completions;

	if (xdata->completed == xdata->queued) {
		goto out;
	}

	towait = xdata->queued - xdata->completed;
	completions = calloc(sizeof(xfer_gd2fs_completion), towait);

	while (towait) {
		int ret = xfer_gd2fs_wait(xctx, completions, towait, -1);
		if (ret < 0) {
			printf("gd2fs: error occurs during cleanup");
			break;
		}

		towait -= ret;
	}

	free(completions);

out:
	xfer_gd2fs_close(xctx);
	free(xdata);
}

FIO_STATIC struct ioengine_ops ioengine = {
	.name = "gd2fs",
	.version= FIO_IOOPS_VERSION,
	.flags = FIO_DISKLESSIO | FIO_NODISKUTIL,
	.setup = fio_gd2fs_setup,
	.init = fio_gd2fs_init,
	.post_init = fio_gd2fs_post_init,
	.open_file = fio_gd2fs_open_file,
	.queue = fio_gd2fs_queue,
	.event = fio_gd2fs_event,
	.getevents = fio_gd2fs_getevents,
	.option_struct_size = sizeof(struct gd2fs_options),
	.options = options,
	.cleanup = fio_gd2fs_cleanup,
};

static void fio_init fio_gd2fs_register(void)
{
	register_ioengine(&ioengine);
}

static void fio_exit fio_gd2fs_unregister(void)
{
	unregister_ioengine(&ioengine);
}
