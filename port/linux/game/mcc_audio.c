/* MCC audio normalization. An MCC-owned Vorbis adapter handles codec differences;
 * no Custom Edition state or sound conversion routine is used. A damaged or
 * unsupported permutation rejects the map, rather than quietly silencing it.
 *
 * Where the host converts (mcc_convert.c), each permutation is read here in
 * the map's order and converted on the host's threads; its result is taken
 * in that same order, so the tags and the limits come out as converting them
 * in turn here makes them, and the converted samples stay in the host's
 * memory (host_mcc_audio_read). Elsewhere they are converted here, in turn. */
#include "cseries.h"
#include "errors.h"
#include "sound/sound_definitions.h"
#include "mcc_runtime.h"
#include "mcc_resources.h"
#include "mcc_audio_kernels.h"
#include "mcc_convert.h"
#include "mcc_loading.h"
#include <stdlib.h>
#include <string.h>

#define MCC_AUDIO_BASE 0x50000000u
#define MCC_AUDIO_LIMIT 0x10000000u
#define MCC_AUDIO_STORAGE_LIMIT 0x40000000u

struct mcc_audio_storage { unsigned char *bytes; uint32_t used, capacity, limit, handle; char const *failure; };
/* Cache reads carry the owning sound tag. Its virtual offsets can therefore
 * be reused by other sounds, just as MCC bitmap offsets are scoped by tag. */
struct mcc_audio_tags {
    struct mcc_audio_storage *items;
    uint32_t count,used,capacity,stream_end;
    /* the samples are the host's (mcc_convert.c), slot the tag's index */
    int host;
};

/* The engine's tracked allocator requires each allocation to be strictly
 * below 256 MiB. Rounding the final growth step to that boundary used to
 * assert before the converter could report an exhausted virtual stream.
 * The host keeps its own (bytes NULL); the limits are counted the same. */
static int mcc_audio_reserve(struct mcc_audio_storage *storage,uint32_t size,int host)
{
    uint32_t capacity;
    unsigned char *larger;
    if (storage->limit>=MCC_AUDIO_LIMIT || storage->used>storage->limit || size>storage->limit-storage->used) {
        storage->failure="audio storage limit exceeded (less than 256 MiB per sound; 1 GiB total)";return 0;
    }
    if (storage->used+size<=storage->capacity) return 1;
    capacity=MIN((storage->used+size+0xFFF)&~0xFFFu,storage->limit);
    if (host) {storage->capacity=capacity;return 1;}
    larger=malloc(capacity);
    if (!larger) {storage->failure="not enough memory to grow the converted sound buffer";return 0;}
    if (storage->used) memcpy(larger,storage->bytes,storage->used);
    if (storage->bytes) free(storage->bytes);
    storage->bytes=larger;storage->capacity=capacity;
    return 1;
}

static int mcc_audio_store(struct mcc_audio_storage *storage, struct mcc_pcm const *pcm,
    int channels, uint32_t rate, struct sound_permutation *permutation)
{
    uint32_t frames=0,size=mcc_ima_encoded_size(pcm,channels,rate,&frames);
    if (!size) {storage->failure="resampled sound exceeds the 16777216-frame limit or is empty";return 0;}
    if (!mcc_audio_reserve(storage,size,FALSE)) return 0;
    mcc_ima_encode(pcm,channels,rate,frames,storage->bytes+storage->used,mcc_loading_pulse);
    permutation->samples.file_offset=MCC_AUDIO_BASE+storage->used;
    permutation->samples.size=size;
    permutation->sample_buffer_size=0;
    permutation->compression=1;
    storage->used+=size;
    return 1;
}

/* Keep validated external ADPCM byte-for-byte, avoiding a lossy round trip. */
static int mcc_audio_store_raw(struct mcc_audio_storage *storage,
    unsigned char const *bytes,struct sound_permutation *permutation)
{
    uint32_t size=(uint32_t)permutation->samples.size;
    if (!mcc_audio_reserve(storage,size,FALSE)) return 0;
    memcpy(storage->bytes+storage->used,bytes,size);
    permutation->samples.file_offset=MCC_AUDIO_BASE+storage->used;
    permutation->samples.pad&=~1;
    storage->used+=size;
    return 1;
}

/* what failed, for the message that refuses the map. Taken on the host's
 * threads only once what came before it is, so it names its own tag and
 * place rather than leaving them in runtime->diagnostic (names: the tag
 * data's, still loaded when it is taken) */
struct mcc_audio_failure {
    uint32_t index, handle, offset;
    char const *tag_name, *stage, *range_name, *permutation_name;
    long range, permutation, codec, size;
    long value; /* the header field refused (NONE: none) */
};

static char const *mcc_audio_codec_name(long codec)
{
    switch (codec) {
    case 0: return "16-bit PCM";
    case 1: return "Xbox ADPCM";
    case 2: return "IMA ADPCM (unsupported)";
    case 3: return "Ogg Vorbis";
    default: return "unknown";
    }
}

static void mcc_audio_failure_reset(struct mcc_audio_failure *failure,uint32_t handle)
{
    failure->handle=handle;
    failure->range=failure->permutation=failure->codec=failure->value=NONE;
    failure->size=0;failure->offset=0;
    failure->range_name=failure->permutation_name=NULL;
    failure->stage="sound header";
}

static void mcc_audio_refuse(struct mcc_runtime *runtime,struct mcc_audio_failure const *failure)
{
    mcc_diagnostic_tag(runtime,failure->handle);
    if (failure->permutation!=NONE)
        mcc_diagnostic_at(runtime,"pitch_ranges[%ld] '%.32s'.permutations[%ld] '%.32s'",
            failure->range,failure->range_name ? failure->range_name : "(unavailable)",
            failure->permutation,failure->permutation_name ? failure->permutation_name : "(unavailable)");
    else if (failure->range!=NONE) mcc_diagnostic_at(runtime,"pitch_ranges[%ld]",failure->range);
    else mcc_diagnostic_at(runtime,"sound header");
    if (failure->value!=NONE) mcc_diagnostic_fail(runtime,"%s (found %ld)",failure->stage,failure->value);
    else mcc_diagnostic_fail(runtime,"%s; codec %ld (%s), payload offset 0x%08lx, %ld bytes",
        failure->stage,failure->codec,mcc_audio_codec_name(failure->codec),
        (unsigned long)failure->offset,failure->size);
    if (!runtime->failure[0]) snprintf(runtime->failure,sizeof(runtime->failure),
        "Sound conversion failed (codec %ld). See debug.txt for the sound name.",failure->codec);
}

/* the sound's header refused: 0 where it is one this converts */
static int mcc_audio_header_refused(struct mcc_audio_failure *failure,struct sound_definition const *sound)
{
    if (!sound) {failure->stage="sound header is outside loaded tag data";return 1;}
    if (sound->encoding<0 || sound->encoding>1) {
        failure->stage="unsupported encoding (expected 0=mono or 1=stereo)";
        failure->value=sound->encoding;return 1;
    }
    if (sound->sample_rate<NONE || sound->sample_rate>1) {
        failure->stage="unsupported sample_rate (expected -1=unspecified, 0=22050 Hz or 1=44100 Hz)";
        failure->value=sound->sample_rate;return 1;
    }
    return 0;
}

/* the permutation's header taken into failure: 0 where it is refused */
static int mcc_audio_permutation_header(struct mcc_audio_failure *failure,
    struct sound_pitch_range const *range,uint32_t permutation_index,struct sound_permutation const *permutation)
{
    failure->permutation=permutation_index;failure->stage="permutation header";
    failure->range_name=range->name;
    failure->permutation_name=permutation ? permutation->name : NULL;
    failure->codec=permutation ? permutation->compression : NONE;
    failure->size=permutation ? permutation->samples.size : 0;
    failure->offset=permutation ? (uint32_t)permutation->samples.file_offset : 0;
    if (!permutation) {failure->stage="permutation header is outside loaded tag data";return 0;}
    if (permutation->samples.size<0 || permutation->samples.size>0x4000000) {
        failure->stage="sample payload size is outside 0..67108864 bytes";
        failure->value=permutation->samples.size;return 0;
    }
    return 1;
}

static int mcc_audio_prepare_serial(struct mcc_runtime *runtime,struct mcc_audio_tags *audio)
{
    struct mcc_audio_storage *storage;
    uint32_t index,converted=0;
    struct mcc_audio_failure failure;
    mcc_audio_failure_reset(&failure,UINT32_MAX);failure.tag_name="(unavailable)";
    for (index=0;index<runtime->report.tag_count;index++) {
        uint32_t *entry=(uint32_t *)(runtime->tag_index+index*0x20);
        struct sound_definition *sound;
        uint32_t range_index,rate,output_rate;
        int channels;
        failure.index=index;
        mcc_loading_update(MCC_LOADING_AUDIO, index, runtime->report.tag_count);
        if (entry[0]!=(uint32_t)SOUND_DEFINITION_TAG) continue;
        storage=&audio->items[index];storage->handle=entry[3];
        storage->limit=MIN(MCC_AUDIO_LIMIT-1u,MCC_AUDIO_STORAGE_LIMIT-audio->capacity);
        mcc_audio_failure_reset(&failure,entry[3]);
        failure.tag_name=mcc_runtime_pointer(runtime,entry[4],256);
        if (!failure.tag_name || !memchr(failure.tag_name,0,256)) failure.tag_name="(unavailable)";
        sound=mcc_runtime_pointer(runtime,entry[5],sizeof(*sound));
        if (mcc_audio_header_refused(&failure,sound)) goto failed;
        /* Extended's empty dialogue tags use NONE for the sample rate.
         * Normalize their unused header, but never guess a payload's rate. */
        channels=sound->encoding+1; rate=sound->sample_rate==1 ? 44100 : 22050;
        output_rate=channels==1 ? 22050 : rate;
        for (range_index=0;range_index<(uint32_t)sound->pitch_ranges.count;range_index++) {
            struct sound_pitch_range *range=mcc_runtime_pointer(runtime,
                (uint32_t)sound->pitch_ranges.address+range_index*sizeof(*range),sizeof(*range));
            uint32_t permutation_index;
            failure.range=range_index;failure.permutation=NONE;failure.stage="pitch range is outside loaded tag data";
            if (!range) goto failed;
            for (permutation_index=0;permutation_index<(uint32_t)range->permutations.count;permutation_index++) {
                struct sound_permutation *permutation=mcc_runtime_pointer(runtime,
                    (uint32_t)range->permutations.address+permutation_index*sizeof(*permutation),sizeof(*permutation));
                unsigned char *data;
                struct mcc_pcm pcm;
                int ok,external;
                char resource_name[288],group_name[288];
                mcc_loading_pulse();
                if (!mcc_audio_permutation_header(&failure,range,permutation_index,permutation)) goto failed;
                external=(permutation->samples.pad&1)!=0;
                permutation->unknown0=NONE;
                permutation->unknown1=0;
                permutation->unknown2=entry[3];
                permutation->unknown3=entry[3];
                permutation->samples.address=NULL;
                permutation->sample_buffer_size=0;
                if (!permutation->samples.size) continue;
                failure.stage="nonempty sound has unspecified sample rate";
                if (sound->sample_rate==NONE) goto failed;
                data=malloc(permutation->samples.size);
                failure.stage="not enough memory to read the source sound payload";
                if (!data) goto failed;
                failure.stage="embedded sound payload could not be read from the map";
                if (external) {
                    failure.stage="external samples (selected MCC sounds.map data does not match)";
                    snprintf(resource_name,sizeof(resource_name),"%s__%lu__%lu",failure.tag_name,range_index,permutation_index);
                    snprintf(group_name,sizeof(group_name),"%s__permutations",failure.tag_name);
                    ok=mcc_resources_read(runtime,2,resource_name,permutation->samples.file_offset,permutation->samples.size,data,group_name);
                } else ok=mcc_runtime_read(runtime,permutation->samples.file_offset,permutation->samples.size,data);
                if (!ok) {
                    free(data); goto failed;
                }
                failure.stage="codec decode failed";
                storage->failure=NULL;
                ok=mcc_pcm_open(data,permutation->samples.size,permutation->compression,channels,rate,&pcm);
                if (!ok && pcm.failure) failure.stage=pcm.failure;
                if (ok && (permutation->compression!=1 || rate!=output_rate)) {
                    failure.stage="ADPCM encoding, allocation, or 256 MiB per-sound or 1 GiB total audio limit";
                    ok=mcc_audio_store(storage,&pcm,channels,output_rate,permutation);
                    if (!ok && storage->failure) failure.stage=storage->failure;
                    if (ok) converted++;
                } else if (ok && external) {
                    failure.stage="external ADPCM allocation or 256 MiB per-sound or 1 GiB total audio limit";
                    ok=mcc_audio_store_raw(storage,data,permutation);
                    if (!ok && storage->failure) failure.stage=storage->failure;
                    if (ok) converted++;
                }
                free(data);
                if (ok) permutation->samples.pad&=~1;
                mcc_pcm_close(&pcm);
                if (!ok) {
                    error(_error_silent,"mcc sound payload: codec %d, %d channels, %lu Hz, offset 0x%08lx, %ld bytes, %s, decoder error %d",
                        permutation->compression,channels,(unsigned long)rate,(unsigned long)permutation->samples.file_offset,
                        permutation->samples.size,external ? "external sounds.map" : "embedded in map",pcm.decoder_error);
                    goto failed;
                }
            }
        }
        sound->compression=1;
        sound->sample_rate=output_rate==44100 ? 1 : 0;
        audio->used+=storage->used;audio->capacity+=storage->capacity;
        audio->stream_end=MAX(audio->stream_end,storage->used);
    }
    error(_error_silent,"mcc audio: %lu permutations converted, %lu ADPCM bytes",converted,audio->used);
    return 1;
failed:
    mcc_audio_refuse(runtime,&failure);
    return 0;
}

/* ---------- on the host's threads */

/* what the queue holds, in the map's order */
enum { MCC_AUDIO_SOUND_BEGIN, MCC_AUDIO_PERMUTATION, MCC_AUDIO_SOUND_END };
/* at most this many in hand, and this many bytes of input read for them */
#define MCC_AUDIO_QUEUE 96
#define MCC_AUDIO_QUEUE_BYTES 0x3000000u

struct mcc_audio_entry {
    int kind, job;
    uint32_t mode, output_rate, entry_handle;
    unsigned char *data;
    uint32_t data_size;
    struct sound_definition *sound;
    struct sound_permutation *permutation;
    struct mcc_audio_failure failure;
    struct mcc_convert_job request;
};

struct mcc_audio_queue {
    struct mcc_audio_entry entries[MCC_AUDIO_QUEUE];
    uint32_t first, count, bytes, converted;
};

static struct mcc_audio_entry *mcc_audio_push(struct mcc_audio_queue *queue)
{
    struct mcc_audio_entry *entry=&queue->entries[(queue->first+queue->count)%MCC_AUDIO_QUEUE];
    memset(entry,0,sizeof(*entry));
    entry->job=-1;
    queue->count++;
    return entry;
}

/* the queue's first taken: its result written where one thread converting
in turn wrote it. 0, with the map refused, where it failed */
static int mcc_audio_commit(struct mcc_runtime *runtime,struct mcc_audio_tags *audio,struct mcc_audio_queue *queue)
{
    struct mcc_audio_entry *entry=&queue->entries[queue->first];
    struct mcc_audio_storage *storage=&audio->items[entry->failure.index];
    int ok=1;
    if (entry->kind==MCC_AUDIO_SOUND_BEGIN) {
        storage->handle=entry->entry_handle;
        storage->limit=MIN(MCC_AUDIO_LIMIT-1u,MCC_AUDIO_STORAGE_LIMIT-audio->capacity);
    } else if (entry->kind==MCC_AUDIO_SOUND_END) {
        entry->sound->compression=1;
        entry->sound->sample_rate=entry->output_rate==44100 ? 1 : 0;
        audio->used+=storage->used;audio->capacity+=storage->capacity;
        audio->stream_end=MAX(audio->stream_end,storage->used);
    } else {
        int result=mcc_convert_finish(entry->job);
        struct sound_permutation *permutation=entry->permutation;
        if (result<0) {
            entry->failure.stage=result==MCC_CONVERT_FAILED_DECODE ? "codec decode failed (on the host's threads)" :
                entry->mode==MCC_AUDIO_COPY ? "external ADPCM allocation or 256 MiB per-sound or 1 GiB total audio limit" :
                "ADPCM encoding, allocation, or 256 MiB per-sound or 1 GiB total audio limit";
            ok=0;
        } else if (entry->mode!=MCC_AUDIO_VALIDATE) {
            uint32_t size=mcc_convert_output_size(entry->job);
            entry->failure.stage=entry->mode==MCC_AUDIO_COPY ?
                "external ADPCM allocation or 256 MiB per-sound or 1 GiB total audio limit" :
                "ADPCM encoding, allocation, or 256 MiB per-sound or 1 GiB total audio limit";
            storage->failure=NULL;
            if (!size || !mcc_audio_reserve(storage,size,TRUE) ||
                mcc_convert_audio_append(entry->job,entry->failure.index)!=(long long)storage->used) {
                if (storage->failure) entry->failure.stage=storage->failure;
                ok=0;
            }
            else {
                permutation->samples.file_offset=MCC_AUDIO_BASE+storage->used;
                if (entry->mode==MCC_AUDIO_ENCODE) {
                    permutation->samples.size=size;
                    permutation->sample_buffer_size=0;
                    permutation->compression=1;
                } else permutation->samples.pad&=~1;
                storage->used+=size;
                queue->converted++;
            }
        }
        if (ok) permutation->samples.pad&=~1;
    }
    mcc_convert_release(entry->job);
    if (entry->data) free(entry->data);
    queue->bytes-=entry->data_size;
    queue->first=(queue->first+1)%MCC_AUDIO_QUEUE;
    queue->count--;
    if (!ok) mcc_audio_refuse(runtime,&entry->failure);
    return ok;
}

/* everything in hand let go, unconverted (the map is refused) */
static void mcc_audio_abandon(struct mcc_audio_queue *queue)
{
    while (queue->count) {
        struct mcc_audio_entry *entry=&queue->entries[queue->first];
        mcc_convert_release(entry->job);
        if (entry->data) free(entry->data);
        queue->first=(queue->first+1)%MCC_AUDIO_QUEUE;
        queue->count--;
    }
    queue->bytes=0;
}

/* every entry taken in turn: 0 if one failed (what follows it let go) */
static int mcc_audio_drain(struct mcc_runtime *runtime,struct mcc_audio_tags *audio,struct mcc_audio_queue *queue)
{
    while (queue->count)
        if (!mcc_audio_commit(runtime,audio,queue)) {mcc_audio_abandon(queue);return 0;}
    return 1;
}

static int mcc_audio_prepare_threaded_with(struct mcc_runtime *runtime,struct mcc_audio_tags *audio,
    struct mcc_audio_queue *queue)
{
    uint32_t index;
    struct mcc_audio_failure failure;
    audio->host=TRUE;
    mcc_audio_failure_reset(&failure,UINT32_MAX);failure.tag_name="(unavailable)";
    for (index=0;index<runtime->report.tag_count;index++) {
        uint32_t *entry=(uint32_t *)(runtime->tag_index+index*0x20);
        struct sound_definition *sound;
        struct mcc_audio_entry *queued;
        uint32_t range_index,rate,output_rate;
        int channels;
        failure.index=index;
        mcc_loading_update(MCC_LOADING_AUDIO, index, runtime->report.tag_count);
        if (entry[0]!=(uint32_t)SOUND_DEFINITION_TAG) continue;
        mcc_audio_failure_reset(&failure,entry[3]);
        failure.tag_name=mcc_runtime_pointer(runtime,entry[4],256);
        if (!failure.tag_name || !memchr(failure.tag_name,0,256)) failure.tag_name="(unavailable)";
        sound=mcc_runtime_pointer(runtime,entry[5],sizeof(*sound));
        if (mcc_audio_header_refused(&failure,sound)) goto failed;
        channels=sound->encoding+1; rate=sound->sample_rate==1 ? 44100 : 22050;
        output_rate=channels==1 ? 22050 : rate;
        if (queue->count==MCC_AUDIO_QUEUE && !mcc_audio_commit(runtime,audio,queue)) {mcc_audio_abandon(queue);return 0;}
        queued=mcc_audio_push(queue);
        queued->kind=MCC_AUDIO_SOUND_BEGIN;queued->failure=failure;queued->entry_handle=entry[3];
        for (range_index=0;range_index<(uint32_t)sound->pitch_ranges.count;range_index++) {
            struct sound_pitch_range *range=mcc_runtime_pointer(runtime,
                (uint32_t)sound->pitch_ranges.address+range_index*sizeof(*range),sizeof(*range));
            uint32_t permutation_index;
            failure.range=range_index;failure.permutation=NONE;failure.stage="pitch range is outside loaded tag data";
            if (!range) goto failed;
            for (permutation_index=0;permutation_index<(uint32_t)range->permutations.count;permutation_index++) {
                struct sound_permutation *permutation=mcc_runtime_pointer(runtime,
                    (uint32_t)range->permutations.address+permutation_index*sizeof(*permutation),sizeof(*permutation));
                unsigned char *data;
                int ok,external;
                char resource_name[288],group_name[288];
                mcc_loading_pulse();
                if (!mcc_audio_permutation_header(&failure,range,permutation_index,permutation)) goto failed;
                external=(permutation->samples.pad&1)!=0;
                permutation->unknown0=NONE;
                permutation->unknown1=0;
                permutation->unknown2=entry[3];
                permutation->unknown3=entry[3];
                permutation->samples.address=NULL;
                permutation->sample_buffer_size=0;
                if (!permutation->samples.size) continue;
                failure.stage="nonempty sound has unspecified sample rate";
                if (sound->sample_rate==NONE) goto failed;
                /* (room for it: the oldest taken first, as many as it needs) */
                while (queue->count && (queue->count>=MCC_AUDIO_QUEUE-1 ||
                    queue->bytes+(uint32_t)permutation->samples.size>MCC_AUDIO_QUEUE_BYTES))
                    if (!mcc_audio_commit(runtime,audio,queue)) {mcc_audio_abandon(queue);return 0;}
                data=malloc(permutation->samples.size);
                failure.stage="not enough memory to read the source sound payload";
                if (!data) goto failed;
                failure.stage="embedded sound payload could not be read from the map";
                if (external) {
                    failure.stage="external samples (selected MCC sounds.map data does not match)";
                    snprintf(resource_name,sizeof(resource_name),"%s__%lu__%lu",failure.tag_name,range_index,permutation_index);
                    snprintf(group_name,sizeof(group_name),"%s__permutations",failure.tag_name);
                    ok=mcc_resources_read(runtime,2,resource_name,permutation->samples.file_offset,permutation->samples.size,data,group_name);
                } else ok=mcc_runtime_read(runtime,permutation->samples.file_offset,permutation->samples.size,data);
                if (!ok) {
                    free(data); goto failed;
                }
                queued=mcc_audio_push(queue);
                queued->kind=MCC_AUDIO_PERMUTATION;
                queued->data=data;queued->data_size=(uint32_t)permutation->samples.size;
                queued->permutation=permutation;queued->sound=sound;queued->failure=failure;
                queued->mode=permutation->compression!=1 || rate!=output_rate ? MCC_AUDIO_ENCODE :
                    external ? MCC_AUDIO_COPY : MCC_AUDIO_VALIDATE;
                queue->bytes+=queued->data_size;
                queued->request.kind=MCC_CONVERT_AUDIO;
                queued->request.input=(uint32_t)(uintptr_t)data;
                queued->request.input_size=queued->data_size;
                queued->request.parameters[0]=(uint32_t)permutation->compression;
                queued->request.parameters[1]=(uint32_t)channels;
                queued->request.parameters[2]=rate;
                queued->request.parameters[3]=output_rate;
                queued->request.parameters[4]=queued->mode;
                /* (the host full: the oldest taken until it has room) */
                while ((queued->job=mcc_convert_submit(&queued->request))<0 && queue->count>1)
                    if (!mcc_audio_commit(runtime,audio,queue)) {mcc_audio_abandon(queue);return 0;}
                if (queued->job<0) {
                    failure.stage="conversion request";
                    goto failed;
                }
                /* (whatever is done already taken, so that the guest's memory
                for its input goes back as soon as it can) */
                while (queue->count>1 && queue->entries[queue->first].kind!=MCC_AUDIO_PERMUTATION)
                    if (!mcc_audio_commit(runtime,audio,queue)) {mcc_audio_abandon(queue);return 0;}
                while (queue->count>1 && mcc_convert_ready(queue->entries[queue->first].job))
                    if (!mcc_audio_commit(runtime,audio,queue)) {mcc_audio_abandon(queue);return 0;}
            }
        }
        if (queue->count==MCC_AUDIO_QUEUE && !mcc_audio_commit(runtime,audio,queue)) {mcc_audio_abandon(queue);return 0;}
        queued=mcc_audio_push(queue);
        queued->kind=MCC_AUDIO_SOUND_END;queued->failure=failure;queued->sound=sound;queued->output_rate=output_rate;
    }
    if (!mcc_audio_drain(runtime,audio,queue)) return 0;
    error(_error_silent,"mcc audio: %lu permutations converted, %lu ADPCM bytes (on the host's threads)",
        (unsigned long)queue->converted,(unsigned long)audio->used);
    return 1;
failed:
    /* (what came before it first: a failure there is the one one thread
    converting in turn would have met first) */
    if (!mcc_audio_drain(runtime,audio,queue)) return 0;
    mcc_audio_refuse(runtime,&failure);
    return 0;
}

/* (the queue, the guest's heap's rather than its image's) */
static int mcc_audio_prepare_threaded(struct mcc_runtime *runtime,struct mcc_audio_tags *audio)
{
    struct mcc_audio_queue *queue=malloc(sizeof(*queue));
    int result;
    if (!queue) return mcc_diagnostic_fail(runtime,"not enough memory for the sound conversion queue");
    memset(queue,0,sizeof(*queue));
    result=mcc_audio_prepare_threaded_with(runtime,audio,queue);
    free(queue);
    return result;
}

int mcc_audio_prepare(struct mcc_runtime *runtime)
{
    struct mcc_audio_tags *audio;
    uint32_t index;
    mcc_diagnostic_tag(runtime,UINT32_MAX);
    mcc_diagnostic_at(runtime,"sound conversion setup");
    if (runtime->source.size>=MCC_AUDIO_BASE || !runtime->report.tag_count || runtime->report.tag_count>65535)
        return mcc_diagnostic_fail(runtime,"map size or tag count exceeds sound converter limits");
    audio=malloc(sizeof(*audio));
    if (!audio) return mcc_diagnostic_fail(runtime,"not enough memory for sound conversion state");
    memset(audio,0,sizeof(*audio));runtime->audio=audio;
    audio->items=malloc(runtime->report.tag_count*sizeof(*audio->items));
    if (!audio->items) return mcc_diagnostic_fail(runtime,"not enough memory for sound storage table");
    audio->count=runtime->report.tag_count;
    memset(audio->items,0,audio->count*sizeof(*audio->items));
    for (index=0;index<audio->count;index++) audio->items[index].handle=0xFFFFFFFFu;
    return mcc_convert_available() ? mcc_audio_prepare_threaded(runtime,audio) : mcc_audio_prepare_serial(runtime,audio);
}

static struct mcc_audio_storage *mcc_audio_tag_storage(struct mcc_runtime *runtime,long tag)
{
    struct mcc_audio_tags *audio=runtime->audio;
    uint32_t index=(uint32_t)tag&0xFFFFu;
    return tag!=NONE && audio && index<audio->count && audio->items[index].handle==(uint32_t)tag ?
        &audio->items[index] : NULL;
}

int mcc_audio_read(struct mcc_runtime *runtime,long tag,uint32_t offset,uint32_t bytes,void *out)
{
    struct mcc_audio_storage *storage=mcc_audio_tag_storage(runtime,tag);
    struct mcc_audio_tags *audio=runtime->audio;
    if (offset<MCC_AUDIO_BASE || offset>=MCC_AUDIO_BASE+MCC_AUDIO_LIMIT) return -1;
    offset-=MCC_AUDIO_BASE;
    if (!storage || offset>storage->used || bytes>storage->used-offset) return 0;
    if (audio->host) return mcc_convert_audio_read((uint32_t)tag&0xFFFFu,offset,bytes,out);
    if (bytes) memcpy(out,storage->bytes+offset,bytes);
    return 1;
}

int mcc_audio_contains(struct mcc_runtime *runtime,long tag,uint32_t offset,uint32_t bytes)
{
    struct mcc_audio_storage *storage=mcc_audio_tag_storage(runtime,tag);
    return storage && offset>=MCC_AUDIO_BASE && offset-MCC_AUDIO_BASE<=storage->used &&
        bytes<=storage->used-(offset-MCC_AUDIO_BASE);
}

uint32_t mcc_audio_stream_end(struct mcc_runtime *runtime)
{
    struct mcc_audio_tags *audio=runtime->audio;
    return audio && audio->stream_end ? MCC_AUDIO_BASE+audio->stream_end : (uint32_t)runtime->source.size;
}

void mcc_audio_dispose(struct mcc_runtime *runtime)
{
    struct mcc_audio_tags *audio=runtime->audio;
    uint32_t i;
    if (audio) {
        for (i=0;i<audio->count;i++) if (audio->items[i].bytes) free(audio->items[i].bytes);
        if (audio->items) free(audio->items);
        if (audio->host) mcc_convert_reset();
        free(audio);runtime->audio=NULL;
    }
}

/* (port) A permutation's samples (`size` bytes of `compression`, `channels`
 * channels at `rate`) as this build plays them, as mcc_audio_prepare_serial
 * makes them: IMA ADPCM at the native rate (mono at 22,050 Hz, stereo at its
 * own), a buffer of *bytes the caller frees; or, ADPCM at that rate already,
 * NULL with *kept set (the samples are kept as they are). NULL, *kept clear,
 * when they cannot be decoded. The Tag Injector's (mcc_tag_bundle.c) */
unsigned char *mcc_audio_bundle_samples(unsigned char const *data,uint32_t size,int compression,
    int channels,uint32_t rate,uint32_t *bytes,int *kept)
{
    struct mcc_pcm pcm;
    uint32_t output_rate=channels==1 ? 22050 : rate,frames=0,encoded;
    unsigned char *output=NULL;
    *bytes=0;*kept=0;
    if (!mcc_pcm_open(data,size,compression,channels,rate,&pcm)) return NULL;
    if (compression==1 && rate==output_rate) {
        mcc_pcm_close(&pcm);
        *kept=1;
        return NULL;
    }
    encoded=mcc_ima_encoded_size(&pcm,channels,output_rate,&frames);
    output=encoded ? malloc(encoded) : NULL;
    if (output) {
        mcc_ima_encode(&pcm,channels,output_rate,frames,output,NULL);
        *bytes=encoded;
    }
    mcc_pcm_close(&pcm);
    return output;
}
