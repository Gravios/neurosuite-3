/***************************************************************************
                          process_extractleds.c
                             -------------------
    begin                : Wed May 22 2002
    copyright            : (C) 2002 by Ken Harris
    email                : kdharris@andromeda.rutgers.edu
    copyright            : (C) 2002-2015 by Michaël Zugaro
    email                : michael.zugaro@college-de-france.fr

    Substantially revised 2025:
      - Modern FFmpeg 4/5/6 API (replaces vendored libav-11 + SDL1)
      - Iterative flood-fill (replaces recursive — avoids stack overflow)
      - Otsu's method for automatic threshold (replaces fixed threshold)
      - Full-resolution RGB detection plane (replaces subsampled YUV)
      - Spot size filtering (-minsize / -maxsize)
      - Multi-LED color discrimination via HSV hue clustering (-colors N)
      - Per-frame temporal smoothing of spot positions (exponential IIR)
      - Supports any container/codec FFmpeg can decode:
        AVI, MP4, MKV, MOV, H.264, H.265, MJPEG, MPEG-2, VP8/9, ...

    Output format is backward-compatible with the original .spots format:
      frameno  nPoints  meanX  meanY  sdX  sdY  meanLum  meanCr  meanCb

    New output columns are appended (optional, enabled with -color):
      frameno  nPoints  meanX  meanY  sdX  sdY  meanLum  meanCr  meanCb  hue  colorLabel
 ***************************************************************************/

/***************************************************************************
 *   GNU LGPL v2.1 or later — see LICENSE                                  *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <float.h>
#include <limits.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>

/* =========================================================================
 * Configuration / globals
 * ======================================================================= */

#define STRLEN       10000
#define MAX_SPOTS    4096   /* max spots per frame (was unbounded in original) */
#define STACK_INIT   65536  /* initial flood-fill stack capacity              */

static int    Thresh         = -1;   /* -1 = auto (Otsu). 0-255 = fixed      */
static int    simulate       = 0;
static int    show_video_info  = 0;
static int    show_video_info2 = 0;
static int    FrameNo        = 0;
static FILE  *SpotFp         = NULL;
static int    MinSize        = 1;    /* minimum spot area in pixels           */
static int    MaxSize        = INT_MAX; /* maximum spot area in pixels        */
static int    NumColors      = 0;    /* 0 = no color discrimination           */
static float  SmoothAlpha    = 0.0f; /* IIR smoothing coefficient 0=off,      */
                                     /* 0<α<1: pos = α*pos + (1-α)*prev      */
static int    OutputColor    = 0;    /* append hue+label columns              */

/* Pixel classification map */
static uint8_t *PixLabel  = NULL;  /* 0: below thresh  1: candidate  2: visited */

/* =========================================================================
 * Spot statistics
 * ======================================================================= */

typedef struct {
    int    nPoints;
    double xSum, ySum, x2Sum, y2Sum;
    double RSum, GSum, BSum;   /* full-res RGB sums for accurate color */
    double LumSum;             /* Y channel sum (backward compat)      */
    double CrSum, CbSum;       /* kept for output compat               */
} SpotStats;

static void ResetSpot(SpotStats *s) {
    memset(s, 0, sizeof(*s));
}

/* =========================================================================
 * Otsu's method: find optimal binary threshold from a luminance histogram
 * Returns threshold value 0-255.
 * ======================================================================= */

static int otsu_threshold(const uint8_t *lum, int n)
{
    long hist[256] = {0};
    for (int i = 0; i < n; i++) hist[lum[i]]++;

    double total = n;
    double sum   = 0;
    for (int i = 0; i < 256; i++) sum += i * hist[i];

    double sumB = 0, wB = 0, wF = 0;
    double varMax = 0;
    int    threshold = 0;

    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (wB == 0) continue;
        wF = total - wB;
        if (wF == 0) break;
        sumB += t * hist[t];
        double mB = sumB / wB;
        double mF = (sum - sumB) / wF;
        double varBetween = wB * wF * (mB - mF) * (mB - mF);
        if (varBetween > varMax) { varMax = varBetween; threshold = t; }
    }
    return threshold;
}

/* =========================================================================
 * Iterative flood-fill using an explicit stack
 * Avoids recursion-depth stack overflows on large bright regions.
 * ======================================================================= */

typedef struct { int x, y; } Point;
static Point *ff_stack   = NULL;
static int    ff_cap     = 0;

static int ff_push(int *top, int x, int y) {
    if (*top >= ff_cap) {
        int new_cap = ff_cap * 2;
        Point *p = realloc(ff_stack, sizeof(Point) * new_cap);
        if (!p) return 0;
        ff_stack = p; ff_cap = new_cap;
    }
    ff_stack[(*top)++] = (Point){x, y};
    return 1;
}

/*
 * FloodFill: iterative 4-connected fill with a configurable reach radius
 * (FillWidth controls the diamond-shaped neighbourhood, as in the original).
 * Accumulates spot statistics into *s.
 */
static void FloodFill(const uint8_t *lum, const uint8_t *R, const uint8_t *G, const uint8_t *B,
                      const uint8_t *Cr, const uint8_t *Cb,
                      int width, int height,
                      int x0, int y0, SpotStats *s,
                      int FillWidth)
{
    int top = 0;
    ff_push(&top, x0, y0);

    while (top > 0) {
        int x = ff_stack[--top].x;
        int y = ff_stack[top].y;
        if (x < 0 || x >= width || y < 0 || y >= height) continue;
        if (PixLabel[x + y*width] != 1) continue;

        PixLabel[x + y*width] = 2;
        s->nPoints++;
        s->xSum  += x; s->ySum  += y;
        s->x2Sum += (double)x*x; s->y2Sum += (double)y*y;
        s->LumSum += lum[x + y*width];
        /* RGB at full resolution */
        s->RSum += R[x + y*width];
        s->GSum += G[x + y*width];
        s->BSum += B[x + y*width];
        /* YUV420P chroma — half-res, kept for backward compat output */
        s->CrSum += Cr[(x/2) + (y/2)*(width/2)];
        s->CbSum += Cb[(x/2) + (y/2)*(width/2)];

        for (int dx = -FillWidth; dx <= FillWidth; dx++)
            for (int dy = -FillWidth; dy <= FillWidth; dy++)
                if (abs(dx) + abs(dy) <= FillWidth)
                    ff_push(&top, x+dx, y+dy);
    }
}

/* =========================================================================
 * RGB → HSV conversion (H in [0,360), S and V in [0,1])
 * Used for color-based LED discrimination.
 * ======================================================================= */

static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v)
{
    r /= 255.0f; g /= 255.0f; b /= 255.0f;
    float cmax = fmaxf(r, fmaxf(g, b));
    float cmin = fminf(r, fminf(g, b));
    float delta = cmax - cmin;
    *v = cmax;
    *s = (cmax > 1e-6f) ? delta / cmax : 0.0f;
    if (delta < 1e-6f) { *h = 0.0f; return; }
    if      (cmax == r) *h = 60.0f * fmodf((g - b) / delta, 6.0f);
    else if (cmax == g) *h = 60.0f * ((b - r) / delta + 2.0f);
    else                *h = 60.0f * ((r - g) / delta + 4.0f);
    if (*h < 0) *h += 360.0f;
}

/* =========================================================================
 * Simple 1-D k-means on hue values to assign color labels.
 * With NumColors==2 this separates e.g. red vs green LEDs.
 * Returns cluster index 0..NumColors-1.
 * ======================================================================= */

/* Persistent cluster centres across frames (warm start) */
static float color_centers[16] = {0};
static int   color_init_done   = 0;

static int assign_color(float hue, float sat, float val)
{
    if (NumColors <= 0 || sat < 0.25f || val < 0.15f) return -1;

    /* Initialise centres uniformly on first call */
    if (!color_init_done) {
        for (int k = 0; k < NumColors; k++)
            color_centers[k] = 360.0f * k / NumColors;
        color_init_done = 1;
    }

    /* Assign to nearest centre (circular distance on hue wheel) */
    int best = 0;
    float best_d = FLT_MAX;
    for (int k = 0; k < NumColors; k++) {
        float d = fabsf(hue - color_centers[k]);
        if (d > 180.0f) d = 360.0f - d;
        if (d < best_d) { best_d = d; best = k; }
    }
    /* Soft update of centre (online k-means, lr=0.05) */
    float diff = hue - color_centers[best];
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    color_centers[best] += 0.05f * diff;
    if (color_centers[best] < 0) color_centers[best] += 360.0f;
    if (color_centers[best] >= 360.0f) color_centers[best] -= 360.0f;
    return best;
}

/* =========================================================================
 * Per-LED temporal smoothing (exponential IIR on centroid position)
 * Indexed by color label (or single-LED index 0).
 * ======================================================================= */

#define MAX_LED_TRACKS 16
static double smooth_x[MAX_LED_TRACKS];
static double smooth_y[MAX_LED_TRACKS];
static int    smooth_init[MAX_LED_TRACKS] = {0};

static void smooth_position(int label, double *x, double *y)
{
    int idx = (label < 0) ? 0 : label;
    if (idx >= MAX_LED_TRACKS) idx = 0;
    if (!smooth_init[idx] || SmoothAlpha <= 0.0f) {
        smooth_x[idx] = *x; smooth_y[idx] = *y;
        smooth_init[idx] = 1;
    } else {
        smooth_x[idx] = SmoothAlpha * smooth_x[idx] + (1.0 - SmoothAlpha) * (*x);
        smooth_y[idx] = SmoothAlpha * smooth_y[idx] + (1.0 - SmoothAlpha) * (*y);
        *x = smooth_x[idx]; *y = smooth_y[idx];
    }
}

/* =========================================================================
 * Frame processing: threshold → fill → filter → output
 * ======================================================================= */

/*
 * RGB planes are passed for full-res color discrimination.
 * lum / Cr / Cb are still passed for backward-compatible output columns.
 */
static void ProcessFrame(const uint8_t *lum,
                         const uint8_t *R, const uint8_t *G, const uint8_t *B,
                         const uint8_t *Cr, const uint8_t *Cb,
                         int width, int height,
                         const char *VideoFileName)
{
    static int FirstTime = 1;
    static int FillWidth = 3;    /* diamond neighbourhood radius */

    /* --- First-frame initialisation --- */
    if (FirstTime) {
        char SpotFileName[STRLEN];
        int  i, dotpos = 0;
        for (i = 0; i < STRLEN-1 && VideoFileName[i]; i++) {
            SpotFileName[i] = VideoFileName[i];
            if (VideoFileName[i] == '.') dotpos = i;
        }
        SpotFileName[i] = '\0';
        strncpy(SpotFileName + dotpos, ".spots", STRLEN - dotpos - 1);
        char *slash = strrchr(SpotFileName, '/');
        if (slash) memmove(SpotFileName, slash+1, strlen(slash));

        if (!simulate) {
            SpotFp = fopen(SpotFileName, "w");
            if (!SpotFp) {
                fprintf(stderr, "process_extractleds: cannot open %s\n", SpotFileName);
                exit(1);
            }
        }
        PixLabel = malloc((size_t)width * height);
        if (!PixLabel) { fprintf(stderr, "process_extractleds: out of memory\n"); exit(1); }

        ff_cap   = STACK_INIT;
        ff_stack = malloc(sizeof(Point) * ff_cap);
        if (!ff_stack) { fprintf(stderr, "process_extractleds: out of memory\n"); exit(1); }

        FirstTime = 0;
    }

    /* --- Determine threshold --- */
    int thresh = (Thresh >= 0) ? Thresh : otsu_threshold(lum, width * height);

    /* --- Threshold the luminance plane --- */
    for (int i = 0; i < width * height; i++)
        PixLabel[i] = (lum[i] > thresh) ? 1 : 0;

    /* --- Find connected components and write spots --- */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (PixLabel[x + y*width] != 1) continue;

            SpotStats s;
            ResetSpot(&s);
            FloodFill(lum, R, G, B, Cr, Cb, width, height, x, y, &s, FillWidth);

            if (s.nPoints < MinSize || s.nPoints > MaxSize) continue;
            if (s.nPoints == 0) continue;

            double meanX = s.xSum / s.nPoints;
            double meanY = s.ySum / s.nPoints;
            double sdX   = sqrt(fmax(0.0, s.x2Sum/s.nPoints - meanX*meanX));
            double sdY   = sqrt(fmax(0.0, s.y2Sum/s.nPoints - meanY*meanY));

            /* Color discrimination */
            float hue = 0, sat = 0, val = 0;
            int   color_label = -1;
            if (NumColors > 0 || OutputColor) {
                rgb_to_hsv((float)(s.RSum/s.nPoints),
                           (float)(s.GSum/s.nPoints),
                           (float)(s.BSum/s.nPoints),
                           &hue, &sat, &val);
                color_label = assign_color(hue, sat, val);
            }

            /* Temporal smoothing */
            if (SmoothAlpha > 0.0f)
                smooth_position(color_label, &meanX, &meanY);

            if (!simulate && SpotFp) {
                fprintf(SpotFp, "%d %d %f %f %f %f %f %f %f",
                        FrameNo, s.nPoints, meanX, meanY, sdX, sdY,
                        s.LumSum/s.nPoints,
                        s.CrSum/s.nPoints,
                        s.CbSum/s.nPoints);
                if (OutputColor)
                    fprintf(SpotFp, " %.1f %d", hue, color_label);
                fprintf(SpotFp, "\n");
                fflush(SpotFp);
            }
        }
    }
    FrameNo++;
}

/* =========================================================================
 * Main: argument parsing + FFmpeg decode loop
 * ======================================================================= */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] <videofile>\n"
        "\n"
        "Detects bright spots (LEDs) in each video frame and writes\n"
        "position/statistics to <videofile>.spots\n"
        "\n"
        "Supports any video container/codec that FFmpeg can decode:\n"
        "  AVI, MP4, MKV, MOV, H.264, H.265, MJPEG, MPEG-2, VP8/9, ...\n"
        "\n"
        "Detection options:\n"
        "  -t <thresh>     fixed luminance threshold 0-255\n"
        "                  (default: auto via Otsu's method per frame)\n"
        "  -minsize <n>    minimum spot area in pixels (default 1)\n"
        "  -maxsize <n>    maximum spot area in pixels (default unlimited)\n"
        "\n"
        "Color discrimination:\n"
        "  -colors <n>     number of distinct LED colors to discriminate (default 0)\n"
        "                  e.g. -colors 2 separates red and green LEDs\n"
        "  -color          append hue and color-label columns to output\n"
        "\n"
        "Temporal smoothing:\n"
        "  -smooth <alpha> IIR smoothing of centroid position, 0<alpha<1\n"
        "                  (0=off; 0.5=moderate; 0.9=heavy, default 0)\n"
        "\n"
        "Info / compat:\n"
        "  -n              simulate: do not write output to disk\n"
        "  -hide           no-op (backward compatibility)\n"
        "  -i              print video rate and duration (from header), then exit\n"
        "  -i2             print video rate and duration (by counting frames), then exit\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *input_filename = NULL;
    int i, ret;

    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "-n")      == 0) { simulate = 1; }
        else if (strcmp(argv[i], "-hide")   == 0) { /* no-op */ }
        else if (strcmp(argv[i], "-i")      == 0) { show_video_info  = 1; }
        else if (strcmp(argv[i], "-i2")     == 0) { show_video_info2 = 1; }
        else if (strcmp(argv[i], "-color")  == 0) { OutputColor = 1; }
        else if (strcmp(argv[i], "-t") == 0) {
            if (++i >= argc) { fprintf(stderr, "-t requires argument\n"); return 1; }
            Thresh = atoi(argv[i]);
            if (Thresh < 0 || Thresh > 255) { fprintf(stderr, "threshold must be 0-255\n"); return 1; }
        }
        else if (strcmp(argv[i], "-minsize") == 0) {
            if (++i >= argc) { fprintf(stderr, "-minsize requires argument\n"); return 1; }
            MinSize = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "-maxsize") == 0) {
            if (++i >= argc) { fprintf(stderr, "-maxsize requires argument\n"); return 1; }
            MaxSize = atoi(argv[i]);
        }
        else if (strcmp(argv[i], "-colors") == 0) {
            if (++i >= argc) { fprintf(stderr, "-colors requires argument\n"); return 1; }
            NumColors = atoi(argv[i]);
            if (NumColors > 16) { fprintf(stderr, "-colors max 16\n"); return 1; }
            OutputColor = 1;
        }
        else if (strcmp(argv[i], "-smooth") == 0) {
            if (++i >= argc) { fprintf(stderr, "-smooth requires argument\n"); return 1; }
            SmoothAlpha = atof(argv[i]);
            if (SmoothAlpha < 0 || SmoothAlpha >= 1.0f) {
                fprintf(stderr, "-smooth must be in [0, 1)\n"); return 1;
            }
        }
        else if (argv[i][0] != '-') {
            input_filename = argv[i];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            print_usage(argv[0]); return 1;
        }
    }
    if (!input_filename) { print_usage(argv[0]); return 1; }

    /* ------------------------------------------------------------------ */
    /* Open input                                                           */
    /* ------------------------------------------------------------------ */
    AVFormatContext *fmt_ctx = NULL;
    if (avformat_open_input(&fmt_ctx, input_filename, NULL, NULL) < 0) {
        fprintf(stderr, "process_extractleds: cannot open '%s'\n", input_filename); return 1;
    }
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "process_extractleds: cannot find stream info\n"); return 1;
    }

    int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_idx < 0) {
        fprintf(stderr, "process_extractleds: no video stream in '%s'\n", input_filename); return 1;
    }
    AVStream *video_st = fmt_ctx->streams[video_idx];

    /* ------------------------------------------------------------------ */
    /* Info-only modes                                                      */
    /* ------------------------------------------------------------------ */
    if (show_video_info) {
        fprintf(stdout, "VIDEO Average Sampling Rate (Hz)   %f\n",  av_q2d(video_st->avg_frame_rate));
        fprintf(stdout, "VIDEO Duration (s) [Header]        %lf\n", (double)fmt_ctx->duration / AV_TIME_BASE);
        avformat_close_input(&fmt_ctx); return 0;
    }
    if (show_video_info2) {
        int64_t n = 0;
        AVPacket *pkt = av_packet_alloc();
        while (av_read_frame(fmt_ctx, pkt) == 0) {
            if (pkt->stream_index == video_idx) n++;
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        double fps = av_q2d(video_st->avg_frame_rate);
        fprintf(stdout, "VIDEO Average Sampling Rate (Hz)   %f\n",  fps);
        fprintf(stdout, "VIDEO Number of Frames             %" PRId64 "\n", n);
        fprintf(stdout, "VIDEO Duration (s) [COMPUTED]      %lf\n", fps > 0 ? n/fps : 0.0);
        avformat_close_input(&fmt_ctx); return 0;
    }

    /* ------------------------------------------------------------------ */
    /* Open decoder                                                         */
    /* ------------------------------------------------------------------ */
    const AVCodec *codec = avcodec_find_decoder(video_st->codecpar->codec_id);
    if (!codec) { fprintf(stderr, "process_extractleds: unsupported codec\n"); return 1; }
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(codec_ctx, video_st->codecpar) < 0 ||
        avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "process_extractleds: cannot open decoder\n"); return 1;
    }

    int width  = codec_ctx->width;
    int height = codec_ctx->height;

    /*
     * We need two converted frames:
     *   yuv_frame  → YUV420P  (for luminance + subsampled Cr/Cb compat columns)
     *   rgb_frame  → RGB24    (for full-res color discrimination)
     */
    struct SwsContext *sws_yuv = sws_getContext(
        width, height, codec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, NULL, NULL, NULL);
    struct SwsContext *sws_rgb = sws_getContext(
        width, height, codec_ctx->pix_fmt,
        width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_yuv || !sws_rgb) {
        fprintf(stderr, "process_extractleds: cannot create swscale contexts\n"); return 1;
    }

    AVFrame *frame     = av_frame_alloc();
    AVFrame *yuv_frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    AVPacket *pkt      = av_packet_alloc();

    uint8_t *yuv_buf = av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1));
    uint8_t *rgb_buf = av_malloc(av_image_get_buffer_size(AV_PIX_FMT_RGB24,   width, height, 1));
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, yuv_buf, AV_PIX_FMT_YUV420P, width, height, 1);
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buf, AV_PIX_FMT_RGB24,   width, height, 1);

    const char *thresh_str = (Thresh >= 0) ? "fixed" : "auto (Otsu)";
    fprintf(stdout, "Processing %s  (%dx%d, threshold=%s",
            input_filename, width, height, thresh_str);
    if (Thresh >= 0) fprintf(stdout, " %d", Thresh);
    if (NumColors > 0) fprintf(stdout, ", colors=%d", NumColors);
    if (SmoothAlpha > 0) fprintf(stdout, ", smooth=%.2f", SmoothAlpha);
    fprintf(stdout, ")\n");

    /* ------------------------------------------------------------------ */
    /* Decode loop                                                          */
    /* ------------------------------------------------------------------ */
    #define PROCESS_FRAME() do { \
        sws_scale(sws_yuv, (const uint8_t * const *)frame->data, frame->linesize, \
                  0, height, yuv_frame->data, yuv_frame->linesize); \
        sws_scale(sws_rgb, (const uint8_t * const *)frame->data, frame->linesize, \
                  0, height, rgb_frame->data, rgb_frame->linesize); \
        /* RGB24: packed R,G,B per pixel */ \
        const uint8_t *R = rgb_frame->data[0];     \
        const uint8_t *G = rgb_frame->data[0] + 1; \
        const uint8_t *B = rgb_frame->data[0] + 2; \
        ProcessFrame(yuv_frame->data[0], R, G, B,  \
                     yuv_frame->data[2], yuv_frame->data[1], \
                     width, height, input_filename); \
        fprintf(stdout, "%7.1f\b\b\b\b\b\b\b", \
                (double)frame->pts * av_q2d(video_st->time_base)); \
        fflush(stdout); \
    } while(0)

    while (av_read_frame(fmt_ctx, pkt) == 0) {
        if (pkt->stream_index != video_idx) { av_packet_unref(pkt); continue; }
        ret = avcodec_send_packet(codec_ctx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;
        while (avcodec_receive_frame(codec_ctx, frame) == 0) { PROCESS_FRAME(); }
    }

    /* Flush */
    avcodec_send_packet(codec_ctx, NULL);
    while (avcodec_receive_frame(codec_ctx, frame) == 0) { PROCESS_FRAME(); }

    #undef PROCESS_FRAME

    /* Sentinel frame (downstream tools use this to know total frame count) */
    if (!simulate && SpotFp) {
        fprintf(SpotFp, "%d -1 -1 -1 -1 -1 -1 -1 -1\n", FrameNo - 1);
        fflush(SpotFp); fclose(SpotFp);
    }

    fprintf(stdout, "\nDone! (%d frames processed)\n", FrameNo);

    /* Cleanup */
    free(PixLabel); free(ff_stack);
    av_free(yuv_buf); av_free(rgb_buf);
    av_frame_free(&frame); av_frame_free(&yuv_frame); av_frame_free(&rgb_frame);
    av_packet_free(&pkt);
    sws_freeContext(sws_yuv); sws_freeContext(sws_rgb);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return 0;
}
