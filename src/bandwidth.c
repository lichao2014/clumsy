// bandwidthping packet module
#include <stdlib.h>
#include <Windows.h>
#include <stdint.h>

#include "iup.h"
#include "common.h"
#define NAME "bandwidth"

#define BANDWIDTH_MIN  "0"
#define BANDWIDTH_MAX  "99999"
#define BUFFER_MIN "0"
#define BUFFER_MAX "99999"
#define BUFFER_DEFAULT 0

//---------------------------------------------------------------------
// rate stats
//---------------------------------------------------------------------
typedef struct _CRateStats
{
	int32_t initialized;
	uint32_t oldest_index;
	uint32_t oldest_ts;
	int64_t accumulated_count;
	int32_t sample_num;
	int window_size;
	float scale;
	uint32_t *array_sum;
	uint32_t *array_sample;
}	CRateStats;


CRateStats* crate_stats_new(int window_size, float scale);

void crate_stats_delete(CRateStats *rate);

void crate_stats_reset(CRateStats *rate);

// call when packet arrives, count is the packet size in bytes
void crate_stats_update(CRateStats *rate, int32_t count, uint32_t now_ts);

// calculate rate
int32_t crate_stats_calculate(CRateStats *rate, uint32_t now_ts);

typedef struct
{
	PacketNode* head;
	PacketNode* tail;
	int size;
} PacketQueue;

typedef struct {
	CRateStats* rateStats;
	PacketNode headNode, tailNode;
	PacketQueue buf;
} CBandwidth;

static void queue_init(PacketQueue* q)
{
	q->head->next = q->tail;
	q->tail->prev = q->head;
	q->size = 0;
}

static void queue_free(PacketQueue* q)
{
	PacketNode* pac = q->head->next;
	while (pac != q->tail) {
		freeNode(popNode(pac));
		pac = q->head->next;
	}

	q->size = 0;
}

static void queue_detach(PacketQueue* q, PacketNode* target)
{
	if (q->size == 0)
	{
		return;
	}

	PacketNode* n1 = target->prev;
	PacketNode* n2 = q->head->next;
	PacketNode* n3 = q->tail->prev;

	n1->next = n2;
	n2->prev = n1;

	n3->next = target;
	target->prev = n3;

	queue_init(q);
}

static void bandwidth_init(CBandwidth *bw)
{
	if (bw->rateStats) crate_stats_delete(bw->rateStats);
	bw->rateStats = crate_stats_new(1000, 1000);

	if (bw->buf.head) {
		queue_free(&bw->buf);
	} else {
		bw->buf.head = &bw->headNode;
		bw->buf.tail = &bw->tailNode;
		queue_init(&bw->buf);
	}
}

static void bandwidth_deinit(CBandwidth* bw, PacketNode* tail)
{
	if (bw->rateStats) {
		crate_stats_delete(bw->rateStats);
		bw->rateStats = NULL;
	}

	if (bw->buf.head) {
		queue_detach(&bw->buf, tail);
	}
}


//---------------------------------------------------------------------
// configuration
//---------------------------------------------------------------------
static Ihandle *inboundCheckbox, *outboundCheckbox, *bandwidthInput, *bufInput;

static volatile short bandwidthEnabled = 0,
    bandwidthInbound = 1, bandwidthOutbound = 1,
	bandwidthBuffer = BUFFER_DEFAULT;

static volatile LONG bandwidthLimit = 0; 
static CBandwidth inbound, outbound;

static Ihandle* bandwidthSetupUI() {
    Ihandle *bandwidthControlsBox = IupHbox(
		IupLabel("Buffer(pkts):"),
		bufInput = IupText(NULL),
        inboundCheckbox = IupToggle("Inbound", NULL),
        outboundCheckbox = IupToggle("Outbound", NULL),
        IupLabel("Limit(KB/s):"),
        bandwidthInput = IupText(NULL),
        NULL
    );

    IupSetAttribute(bandwidthInput, "VISIBLECOLUMNS", "4");
    IupSetAttribute(bandwidthInput, "VALUE", "0");
    IupSetCallback(bandwidthInput, "VALUECHANGED_CB", uiSyncInt32);
    IupSetAttribute(bandwidthInput, SYNCED_VALUE, (char*)&bandwidthLimit);
    IupSetAttribute(bandwidthInput, INTEGER_MAX, BANDWIDTH_MAX);
    IupSetAttribute(bandwidthInput, INTEGER_MIN, BANDWIDTH_MIN);
    IupSetCallback(inboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(inboundCheckbox, SYNCED_VALUE, (char*)&bandwidthInbound);
    IupSetCallback(outboundCheckbox, "ACTION", (Icallback)uiSyncToggle);
    IupSetAttribute(outboundCheckbox, SYNCED_VALUE, (char*)&bandwidthOutbound);

	// sync delay time
	IupSetAttribute(bufInput, "VISIBLECOLUMNS", "3");
	IupSetAttribute(bufInput, "VALUE", STR(BUFFER_DEFAULT));
	IupSetCallback(bufInput, "VALUECHANGED_CB", (Icallback)uiSyncInteger);
	IupSetAttribute(bufInput, SYNCED_VALUE, (char*)&bandwidthBuffer);
	IupSetAttribute(bufInput, INTEGER_MAX, BUFFER_MAX);
	IupSetAttribute(bufInput, INTEGER_MIN, BUFFER_MIN);

    // enable by default to avoid confusing
    IupSetAttribute(inboundCheckbox, "VALUE", "ON");
    IupSetAttribute(outboundCheckbox, "VALUE", "ON");

    if (parameterized) {
		setFromParameter(bufInput, "VALUE", NAME"-buffer");
        setFromParameter(inboundCheckbox, "VALUE", NAME"-inbound");
        setFromParameter(outboundCheckbox, "VALUE", NAME"-outbound");
        setFromParameter(bandwidthInput, "VALUE", NAME"-bandwidth");
    }

    return bandwidthControlsBox;
}

static void bandwidthStartUp() {
	bandwidth_init(&inbound);
	bandwidth_init(&outbound);

	startTimePeriod();

    LOG("bandwidth enabled");
}

static void bandwidthCloseDown(PacketNode *head, PacketNode *tail) {
    UNREFERENCED_PARAMETER(head);

	bandwidth_deinit(&inbound, tail);
	bandwidth_deinit(&outbound, tail);

	endTimePeriod();

    LOG("bandwidth disabled");
}


//---------------------------------------------------------------------
// process
//---------------------------------------------------------------------
static short bandwidthProcess(PacketNode *head, PacketNode* tail) {
    int dropped = 0;
	DWORD now_ts = timeGetTime();
	int limit = bandwidthLimit * 1024;

	queue_detach(&inbound.buf, tail);
	queue_detach(&outbound.buf, tail);

	if (limit <= 0) {
		return 0;
	}

	PacketNode* pac = tail->prev;
    while (pac != head) {
        PacketNode *prev = pac->prev;

		CBandwidth* bw = NULL;
		if (IS_INBOUND(pac->addr.Direction) && bandwidthInbound) {
			bw = &inbound;
		} else if (IS_OUTBOUND(pac->addr.Direction) && bandwidthOutbound) {
			bw = &outbound;
		}

		if (bw) {
			int rate = crate_stats_calculate(bw->rateStats, now_ts);
			int size = pac->packetLen;
			if (rate + size > limit) {
				dropped++;

				pac = popNode(pac);
				if (bw->buf.size >= bandwidthBuffer)
				{
					LOG("dropped with bandwidth %dKB/s, direction %s",
						(int)bandwidthLimit, BOUND_TEXT(pac->addr.Direction));
					freeNode(pac);
				}
				else
				{
					LOG("enqueue with dropped %d bufs %d bandwidth %dKB/s, direction %s",
						dropped, bw->buf.size, (int)bandwidthLimit, BOUND_TEXT(pac->addr.Direction));

					insertAfter(pac, bw->buf.head);
					bw->buf.size++;
				}
			} else {
				crate_stats_update(bw->rateStats, size, now_ts);
			}
		}

		pac = prev;
    }

    return dropped > 0;
}


//---------------------------------------------------------------------
// module
//---------------------------------------------------------------------
Module bandwidthModule = {
    "Bandwidth",
    NAME,
    (short*)&bandwidthEnabled,
    bandwidthSetupUI,
    bandwidthStartUp,
    bandwidthCloseDown,
    bandwidthProcess,
    // runtime fields
    0, 0, NULL
};



//---------------------------------------------------------------------
// create new CRateStat
//---------------------------------------------------------------------
CRateStats* crate_stats_new(int window_size, float scale)
{
	CRateStats *rate = (CRateStats*)malloc(sizeof(CRateStats));
	assert(rate);
	rate->array_sum = (uint32_t*)malloc(sizeof(uint32_t) * window_size);
	assert(rate->array_sum);
	rate->array_sample = (uint32_t*)malloc(sizeof(uint32_t) * window_size);
	assert(rate->array_sample);
	rate->window_size = window_size;
	rate->scale = scale;
	crate_stats_reset(rate);
	return rate;
}


//---------------------------------------------------------------------
// delete rate
//---------------------------------------------------------------------
void crate_stats_delete(CRateStats *rate)
{
	if (rate) {
		rate->window_size = 0;
		if (rate->array_sum) free(rate->array_sum);
		if (rate->array_sample) free(rate->array_sample);
		rate->array_sum = NULL;
		rate->array_sample = NULL;
		rate->initialized = 0;
		free(rate);
	}
}


//---------------------------------------------------------------------
// reset rate
//---------------------------------------------------------------------
void crate_stats_reset(CRateStats *rate)
{
	int i;
	for (i = 0; i < rate->window_size; i++) {
		rate->array_sum[i] = 0;
		rate->array_sample[i] = 0;
	}
	rate->initialized = 0;
	rate->sample_num = 0;
	rate->accumulated_count = 0;
	rate->oldest_ts = 0;
	rate->oldest_index = 0;
}


//---------------------------------------------------------------------
// evict oldest history
//---------------------------------------------------------------------
void crate_stats_evict(CRateStats *rate, uint32_t now_ts)
{
	if (rate->initialized == 0) 
		return;

	uint32_t new_oldest_ts = now_ts - ((uint32_t)rate->window_size) + 1;

	if (((int32_t)(new_oldest_ts - rate->oldest_ts)) < 0) 
		return;

	while (((int32_t)(rate->oldest_ts - new_oldest_ts)) < 0) {
		uint32_t index = rate->oldest_index;
		if (rate->sample_num == 0) break;
		rate->sample_num -= rate->array_sample[index];
		rate->accumulated_count -= rate->array_sum[index];
		rate->array_sample[index] = 0;
		rate->array_sum[index] = 0;
		rate->oldest_index++;
		if (rate->oldest_index >= (uint32_t)rate->window_size) {
			rate->oldest_index = 0;
		}
		rate->oldest_ts++;
	}
	assert(rate->sample_num >= 0);
	assert(rate->accumulated_count >= 0);
	rate->oldest_ts = new_oldest_ts;
}


//---------------------------------------------------------------------
// update stats
//---------------------------------------------------------------------
void crate_stats_update(CRateStats *rate, int32_t count, uint32_t now_ts)
{
	if (rate->initialized == 0) {
		rate->oldest_ts = now_ts;
		rate->oldest_index = 0;
		rate->accumulated_count = 0;
		rate->sample_num = 0;
		rate->initialized = 1;
	}

	if (((int32_t)(now_ts - rate->oldest_ts)) < 0) {
		return;
	}

	crate_stats_evict(rate, now_ts);

	int32_t offset = (int32_t)(now_ts - rate->oldest_ts);
	int32_t index = (rate->oldest_index + offset) % rate->window_size;

	rate->sample_num++;
	rate->accumulated_count += count;
	rate->array_sum[index] += count;
	rate->array_sample[index] += 1;
}


//---------------------------------------------------------------------
// calculate
//---------------------------------------------------------------------
int32_t crate_stats_calculate(CRateStats *rate, uint32_t now_ts)
{
	int32_t active_size = (int32_t)(now_ts - rate->oldest_ts + 1);
	float r;

	crate_stats_evict(rate, now_ts);

	if (rate->initialized == 0 || 
		rate->sample_num <= 0 || 
		active_size <= 1 || 
		active_size < rate->window_size) {
		return -1;
	}

	r = ((((float)rate->accumulated_count) * rate->scale) / 
				rate->window_size) + 0.5;

	return (int32_t)r;
}



