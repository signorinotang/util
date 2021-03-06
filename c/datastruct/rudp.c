//
// Created by hujianzhe
//

#include "rudp.h"

#ifdef	__cplusplus
extern "C" {
#endif

static unsigned long long htonll(unsigned long long v) {
	short is_little_endian = 1;
	if (*(unsigned char*)(&is_little_endian)) {
		int i;
		unsigned char* p = (unsigned char*)&v;
		for (i = 0; i < sizeof(v) / 2; ++i) {
			unsigned char temp = p[i];
			p[i] = p[sizeof(v) - i - 1];
			p[sizeof(v) - i - 1] = temp;
		}
	}
	return v;
}
static unsigned long long ntohll(unsigned long long v) {
	short is_little_endian = 1;
	if (*(unsigned char*)(&is_little_endian)) {
		int i;
		unsigned char* p = (unsigned char*)&v;
		for (i = 0; i < sizeof(v) / 2; ++i) {
			unsigned char temp = p[i];
			p[i] = p[sizeof(v) - i - 1];
			p[sizeof(v) - i - 1] = temp;
		}
	}
	return v;
}

enum hdrtype {
	RUDP_HDR_TYPE_DATA,
	RUDP_HDR_TYPE_ACK
};

void rudp_ctx_clean(struct rudp_ctx* ctx) {
	int i;
	if (ctx->free_callback) {
		for (i = 0; i < RUDP_WND_SIZE; ++i) {
			if (ctx->recv_wnd[i].hdr) {
				ctx->free_callback(ctx, ctx->recv_wnd[i].hdr);
			}
			if (ctx->send_wnd[i].hdr) {
				ctx->free_callback(ctx, ctx->send_wnd[i].hdr);
			}
		}
	}
	for (i = 0; i < sizeof(*ctx); ++i) {
		((unsigned char*)ctx)[i] = 0;
	}
}

void rudp_recv_sort_and_ack(struct rudp_ctx* ctx, long long now_timestamp_msec, const struct rudp_hdr* hdr, unsigned short len) {
	unsigned long long hdr_seq = ntohll(hdr->seq);
	switch (hdr->type) {
		case RUDP_HDR_TYPE_DATA:
		{
			int i;
			struct rudp_recv_cache* cache;

			// TODO: try ack
			do {
				struct rudp_hdr ack_hdr = { RUDP_HDR_TYPE_ACK, hdr->seq };
				ctx->send_callback(ctx, &ack_hdr, sizeof(ack_hdr));
			} while (0);

			// check seq is valid
			if (hdr_seq < RUDP_WND_SIZE) {}
			else if (hdr_seq < ctx->recv_seq) {
				break;
			}
			if (hdr_seq - ctx->recv_seq >= RUDP_WND_SIZE) {
				break;
			}

			cache = &ctx->recv_wnd[hdr_seq % RUDP_WND_SIZE];
			if (cache->hdr) {
				// already exist ... 
				break;
			}
			cache->hdr = hdr;

			if (ctx->recv_seq != hdr_seq) {
				// packet disOrder !!!
				break;
			}

			for (i = 0; i < RUDP_WND_SIZE; ++i) {
				cache = &ctx->recv_wnd[ctx->recv_seq % RUDP_WND_SIZE];
				if (!cache->hdr) {
					// wait recv
					break;
				}

				// packet has order !
				ctx->recv_callback(ctx, cache->hdr, cache->len);

				// free packet
				ctx->free_callback(ctx, cache->hdr);

				cache->hdr = (struct rudp_hdr*)0;
				ctx->recv_seq++;
			}

			break;
		}

		case RUDP_HDR_TYPE_ACK:
		{
			int ack_seq = hdr_seq % RUDP_WND_SIZE;
			// ack success and should be free
			struct rudp_send_cache* cache = &ctx->send_wnd[ack_seq];
			if (cache->hdr) {
				if (ntohll(cache->hdr->seq) != hdr_seq) {
					break;
				}
				ctx->free_callback(ctx, cache->hdr);
				cache->hdr = (struct rudp_hdr*)0;
			}
			else {
				break;
			}

			// update send ack_seq
			if (ack_seq == ctx->ack_seq) {
				do {
					++ctx->ack_seq;
					if (ctx->send_wnd[ctx->ack_seq].hdr) {
						break;
					}
				} while (ctx->ack_seq != ctx->send_seq % RUDP_WND_SIZE);
			}
			else if (ctx->ack_seq + 2 <= ack_seq) {
				// fast resend
				cache = &ctx->send_wnd[ctx->ack_seq];
				if (cache->hdr) {
					cache->last_resend_msec = now_timestamp_msec;
					ctx->send_callback(ctx, cache->hdr, cache->len);
				}
			}

			ctx->free_callback(ctx, hdr);

			break;
		}

		default:
		{
			ctx->free_callback(ctx, hdr);
		}
	}
}

int rudp_send(struct rudp_ctx* ctx, long long now_timestamp_msec, struct rudp_hdr* hdr, unsigned short len) {
	int ack_seq = ctx->send_seq % RUDP_WND_SIZE;
	if (ctx->send_wnd[ack_seq].hdr) {
		// packet hasn't be ack
		return -1;
	}
	hdr->type = RUDP_HDR_TYPE_DATA;
	hdr->seq = htonll(ctx->send_seq++);

	// wait ack
	ctx->send_wnd[ack_seq].hdr = hdr;
	ctx->send_wnd[ack_seq].len = len;
	ctx->send_wnd[ack_seq].resend_times = 0;
	ctx->send_wnd[ack_seq].last_resend_msec = now_timestamp_msec;

	// try send
	ctx->send_callback(ctx, hdr, len);

	return 0;
}

int rudp_check_resend(struct rudp_ctx* ctx, long long now_timestamp_msec, int* next_wait_msec) {
	int i;
	*next_wait_msec = -1;
	for (i = 0; i < RUDP_WND_SIZE; ++i) {
		int delta_timelen, rto;
		struct rudp_send_cache* cache = &ctx->send_wnd[(unsigned char)(ctx->ack_seq + i)];
		if (!cache->hdr) {
			// this wnd hasn't packet
			continue;
		}

		// check resend timeout
		delta_timelen = (int)(now_timestamp_msec - cache->last_resend_msec);
		if (delta_timelen < 0) {
			delta_timelen = -delta_timelen;
		}
		if (-1 == *next_wait_msec || delta_timelen < *next_wait_msec) {
			// update next wait millionsecond
			*next_wait_msec = delta_timelen;
		}
		// calculator rto
		rto = ctx->first_resend_interval_msec;
		if (cache->resend_times) {
			rto += ctx->first_resend_interval_msec >> 1;
		}
		if (delta_timelen < rto) {
			continue;
		}

		// check resend times overflow
		if (cache->resend_times + 1 > ctx->max_resend_times) {
			// network maybe lost !
			*next_wait_msec = -1;
			return -1;
		}

		// TODO: try resend
		++cache->resend_times;
		cache->last_resend_msec = now_timestamp_msec;
		ctx->send_callback(ctx, cache->hdr, cache->len);
	}
	return 0;
}

#ifdef	__cplusplus
}
#endif
