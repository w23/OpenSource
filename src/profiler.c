#include "profiler.h"

static struct {
	int cursor;
	struct {
		const char *msg;
		ATimeUs delta;
	} event[65536];
	int frame;
	ATimeUs last_print_time;
	ATimeUs profiler_time;
	ATimeUs frame_deltas, last_frame;
	int counted_frame;
} profiler;

void profilerInit() {
	profiler.cursor = 0;
	profiler.frame = 0;
	profiler.last_print_time = 0;
	profiler.profiler_time = 0;
	profiler.frame_deltas = profiler.last_frame = 0;
	profiler.counted_frame = 0;
}

void profileEvent(const char *msg, ATimeUs delta) {
	ATTO_ASSERT(profiler.cursor < 65536);
	if (profiler.cursor < 0 || profiler.cursor >= COUNTOF(profiler.event))
		return;
	profiler.event[profiler.cursor].msg = msg;
	profiler.event[profiler.cursor].delta = delta;
	++profiler.cursor;
}

typedef struct {
	const char *name;
	int count;
	ATimeUs total_time;
	ATimeUs min_time;
	ATimeUs max_time;
} ProfilerLocation;

int profilerFrame(struct Stack *stack_temp) {
	int retval = 0;
	const ATimeUs start = aAppTime();
	profiler.frame_deltas += start - profiler.last_frame;

	void *tmp_cursor = stackGetCursor(stack_temp);
	const int max_array_size = (int)(stackGetFree(stack_temp) / sizeof(ProfilerLocation));
	int array_size = 0;
	ProfilerLocation *array = tmp_cursor;
	int total_time = 0;
	for (int i = 0; i < profiler.cursor; ++i) {
		ProfilerLocation *loc = 0;
		for (int j = 0; j < array_size; ++j)
			if (array[j].name == profiler.event[i].msg) {
				loc = array + j;
				break;
			}
		if (!loc) {
			ATTO_ASSERT(array_size< max_array_size);
			loc = array + array_size++;
			loc->name = profiler.event[i].msg;
			loc->count = 0;
			loc->total_time = 0;
			loc->min_time = 0x7fffffffu;
			loc->max_time = 0;
		}

		++loc->count;
		const ATimeUs delta = profiler.event[i].delta;
		loc->total_time += delta;
		total_time += delta;
		if (delta < loc->min_time) loc->min_time = delta;
		if (delta > loc->max_time) loc->max_time = delta;
	}

	++profiler.counted_frame;
	++profiler.frame;

	if (start - profiler.last_print_time > 1000000) {
		PRINT("=================================================");
		const ATimeUs dt = profiler.frame_deltas / profiler.counted_frame;
		PRINTF("avg frame = %dus (fps = %f)", dt, 1000000. / dt);
		PRINTF("PROF: frame=%d, total_frame_time=%d total_prof_time=%d, avg_prof_time=%d events=%d unique=%d",
			profiler.frame, total_time, profiler.profiler_time, profiler.profiler_time / profiler.frame,
			profiler.cursor, array_size);

	for (int i = 0; i < array_size; ++i) {
		const ProfilerLocation *loc = array + i;
		PRINTF("T%d: total=%d count=%d min=%d max=%d, avg=%d %s",
				i, loc->total_time, loc->count, loc->min_time, loc->max_time,
				loc->total_time / loc->count, loc->name);
	}

#if 0
#define TOP_N 10
		int max_time[TOP_N] = {0};
		int max_count[TOP_N] = {0};
		for (int i = 0; i < array_size; ++i) {
			const ProfilerLocation *loc = array + i;
			for (int j = 0; j < TOP_N; ++j)
				if (array[max_time[j]].max_time < loc->max_time) {
					for (int k = j + 1; k < TOP_N; ++k) max_time[k] = max_time[k - 1];
					max_time[j] = i;
					break;
				}
			for (int j = 0; j < TOP_N; ++j)
				if (array[max_count[j]].count < loc->count) {
					for (int k = j + 1; k < TOP_N; ++k) max_count[k] = max_count[k - 1];
					max_count[j] = i;
					break;
				}
		}
		if (array_size > TOP_N) {
			for (int i = 0; i < TOP_N; ++i) {
				const ProfilerLocation *loc = array + max_time[i];
				PRINTF("T%d %d: total=%d count=%d min=%d max=%d, avg=%d %s",
						i, max_time[i], loc->total_time, loc->count, loc->min_time, loc->max_time,
						loc->total_time / loc->count, loc->name);
			}
			for (int i = 0; i < TOP_N; ++i) {
				const ProfilerLocation *loc = array + max_count[i];
				PRINTF("C%d %d: total=%d count=%d min=%d max=%d, avg=%d %s",
						i, max_count[i], loc->total_time, loc->count, loc->min_time, loc->max_time,
						loc->total_time / loc->count, loc->name);
			}
		}
#endif

		profiler.last_print_time = start;
		profiler.counted_frame = 0;
		profiler.frame_deltas = 0;
		retval = 1;
	}

	profiler.last_frame = start;
	profiler.profiler_time += aAppTime() - start;
	profiler.cursor = 0;
	profileEvent("PROFILER", aAppTime() - start);
	return retval;
}
