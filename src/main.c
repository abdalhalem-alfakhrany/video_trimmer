#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static char file_name[256];

#define PANIC(condition, msg, ...)         \
    if (condition < 0)                     \
    {                                      \
        fprintf(stderr, msg, __VA_ARGS__); \
        return condition;                  \
    }

#define PANIC_RET(condition, ret, msg, ...) \
    if (condition)                          \
    {                                       \
        fprintf(stderr, msg, __VA_ARGS__);  \
        return ret;                         \
    }



// s 10 : 20
// s 30 : 40
// s 40 : end
int main(int argc, char *argv[])
{
    AVFormatContext *input_ctx = NULL, *output_ctx = NULL;
    AVStream *video_in_stream = NULL, *audio_in_stream = NULL, *video_out_stream = NULL, *audio_out_stream = NULL;
    AVCodecParameters *video_in_codecpar = NULL, *audio_in_codecpar = NULL;
    AVPacket *packet = NULL;
    int ret, video_stream_idx = -1, audio_stream_idx = -1;

    PANIC_RET(argc < 3, 1, "Usage: %s <input file> <output file>\n", argv[0])
    PANIC(avformat_open_input(&input_ctx, argv[1], NULL, NULL), "Could not open input file '%s'\n", argv[1])
    PANIC(avformat_find_stream_info(input_ctx, NULL), "Failed to get input stream information\n", "")

    for (int i = 0; i < input_ctx->nb_streams; i++)
    {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_idx = i;

        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            audio_stream_idx = i;
    }

    avformat_alloc_output_context2(&output_ctx, NULL, NULL, argv[2]);

    PANIC_RET(!output_ctx, AVERROR(ENOMEM), "Could not create output context\n", "")
    PANIC_RET(video_stream_idx == -1, AVERROR(EINVAL), "No video stream found in input file\n", "")
    PANIC_RET(audio_stream_idx == -1, AVERROR(EINVAL), "No audio stream found in input file\n", "")

    video_in_stream = input_ctx->streams[video_stream_idx];
    video_in_codecpar = video_in_stream->codecpar;
    PANIC_RET(!(video_out_stream = avformat_new_stream(output_ctx, NULL)), AVERROR(ENOMEM), "Failed to create output stream\n", "")
    PANIC(avcodec_parameters_copy(video_out_stream->codecpar, video_in_codecpar), "Failed to copy codec parameters\n", "")
    video_out_stream->codecpar->codec_tag = video_out_stream->codecpar->codec_tag;

    audio_in_stream = input_ctx->streams[audio_stream_idx];
    audio_in_codecpar = audio_in_stream->codecpar;
    PANIC_RET(!(audio_out_stream = avformat_new_stream(output_ctx, NULL)), AVERROR(ENOMEM), "Failed to create output stream\n", "")
    PANIC(avcodec_parameters_copy(audio_out_stream->codecpar, audio_in_codecpar), "Failed to copy codec parameters\n", "")
    audio_out_stream->codecpar->codec_tag = audio_out_stream->codecpar->codec_tag;

    if (!(output_ctx->oformat->flags & AVFMT_NOFILE))
        PANIC(avio_open(&output_ctx->pb, argv[2], AVIO_FLAG_WRITE), "Could not open output file '%s'\n", argv[2])

    PANIC(avformat_write_header(output_ctx, NULL), "Error writing header to output file\n", "")

    PANIC_RET(!(packet = av_packet_alloc()), AVERROR(ENOMEM), "Could not allocate packet\n", "");

    bool video_started = false;
    bool audio_started = false;

    size_t start_time_seconds = 4;

    int64_t video_start_pts = AV_NOPTS_VALUE;
    int64_t audio_start_pts = AV_NOPTS_VALUE;

    if (video_stream_idx != -1)
        video_start_pts = av_rescale_q((int64_t)(start_time_seconds * AV_TIME_BASE),
                                       AV_TIME_BASE_Q,
                                       video_in_stream->time_base);

    if (audio_stream_idx != -1)
        audio_start_pts = av_rescale_q((int64_t)(start_time_seconds * AV_TIME_BASE),
                                       AV_TIME_BASE_Q,
                                       audio_in_stream->time_base);

    int64_t last_pts = AV_NOPTS_VALUE;

    while (av_read_frame(input_ctx, packet) >= 0)
    {

        if (packet->stream_index == video_stream_idx)
        {
            if (!video_started)
            {
                if (packet->pts != AV_NOPTS_VALUE && packet->pts >= video_start_pts)
                {
                    video_started = 1;
                }
                else
                {
                    av_packet_unref(packet);
                    continue;
                }
            }

            av_packet_rescale_ts(packet, video_in_stream->time_base, video_out_stream->time_base);
            packet->stream_index = video_stream_idx;

            if ((ret = av_interleaved_write_frame(output_ctx, packet)) < 0)
            {
                fprintf(stderr, "Error writing frame to output file\n");
                break;
            }
        }
        else if (packet->stream_index == audio_stream_idx)
        {
            if (!audio_started)
            {
                if (packet->pts != AV_NOPTS_VALUE && packet->pts >= audio_start_pts)
                {
                    audio_started = 1;
                }
                else
                {
                    av_packet_unref(packet);
                    continue;
                }
            }
            av_packet_rescale_ts(packet, audio_in_stream->time_base, audio_out_stream->time_base);
            packet->stream_index = audio_stream_idx;

            if ((ret = av_interleaved_write_frame(output_ctx, packet)) < 0)
            {
                fprintf(stderr, "Error writing frame to output file\n");
                break;
            }
            last_pts = packet->pts;
        }
        av_packet_unref(packet);
    }

    if (last_pts != AV_NOPTS_VALUE)
    {
        if (video_out_stream)
            video_out_stream->duration = last_pts;
        if (audio_out_stream)
            audio_out_stream->duration = last_pts;
    }

    av_write_trailer(output_ctx);

    av_packet_free(&packet);
    if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_ctx->pb);
    avformat_free_context(output_ctx);
    avformat_close_input(&input_ctx);

    printf("Successfully processed video file\n");
    return 0;
}