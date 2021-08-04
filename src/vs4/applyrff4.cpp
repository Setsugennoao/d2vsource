/*
 * VapourSynth D2V Plugin
 *
 * Copyright (c) 2012 Derek Buitenhuis
 *
 * This file is part of d2vsource.
 *
 * d2vsource is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * d2vsource is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with d2vsource; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <cstdint>
#include <cstdlib>

#include <algorithm>

#include <VapourSynth4.h>
#include <VSHelper4.h>

#include "applyrff4.hpp"
#include "d2v.hpp"
#include "gop.hpp"

namespace vs4 {

static const VSFrame *VS_CC rffGetFrame(int n, int activationReason, void *instanceData, void **frameData,
                                    VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    const rffData *d = (const rffData *) instanceData;
    VSFrame *f;

    /* What frames to use for fields. */
    const rffField *top_field = &d->fields[n * 2];
    const rffField *bottom_field = &d->fields[n * 2 + 1];
    if (top_field->type == Bottom)
        std::swap(top_field, bottom_field);

    int top    = top_field->frame;
    int bottom = bottom_field->frame;

    bool samefields = top == bottom;

    /* Request out source frames. */
    if (activationReason == arInitial) {
        if (samefields) {
            vsapi->requestFrameFilter(top, d->node, frameCtx);
        } else {
            vsapi->requestFrameFilter(min(top, bottom), d->node, frameCtx);
            vsapi->requestFrameFilter(max(top, bottom), d->node, frameCtx);
        }
        return NULL;
    }

    /* Check if we're ready yet. */
    if (activationReason != arAllFramesReady)
        return NULL;

    /* Source and destination frames. */
    const VSFrame *st = vsapi->getFrameFilter(top, d->node, frameCtx);
    const VSFrame *sb = samefields ? NULL : vsapi->getFrameFilter(bottom, d->node, frameCtx);

    /* Copy into VS's buffers. */
    if (samefields) {
        f = vsapi->copyFrame(st, core);
    } else {
        ptrdiff_t dst_stride[3], srct_stride[3], srcb_stride[3];

        /*
         * Copy properties from the first field's source frame.
         * Some of them will be wrong for this frame, but ¯\_(ツ)_/¯.
        */
        const VSFrame *prop_src = bottom_field < top_field ? sb : st;

        f  = vsapi->newVideoFrame(&d->vi.format, d->vi.width, d->vi.height, prop_src, core);

        for (int i = 0; i < d->vi.format.numPlanes; i++) {
            dst_stride[i]  = vsapi->getStride(f, i);
            srct_stride[i] = vsapi->getStride(st, i);
            srcb_stride[i] = vsapi->getStride(sb, i);

            uint8_t *dstp = vsapi->getWritePtr(f, i);
            const uint8_t *srctp = vsapi->getReadPtr(st, i);
            const uint8_t *srcbp = vsapi->getReadPtr(sb, i);
            int width = vsapi->getFrameWidth(f, i);
            int height = vsapi->getFrameHeight(f, i);

            vsh::bitblt(dstp, dst_stride[i] * 2,
                      srctp, srct_stride[i] * 2,
                      width * d->vi.format.bytesPerSample, height / 2);

            vsh::bitblt(dstp + dst_stride[i], dst_stride[i] * 2,
                      srcbp + srcb_stride[i], srcb_stride[i] * 2,
                      width * d->vi.format.bytesPerSample, height / 2);
        }
    }

    if (!samefields) {
        /* Set field order. */
        VSMap *props = vsapi->getFramePropertiesRW(f);

        vsapi->mapSetInt(props, "_FieldBased", (bottom_field < top_field) ? 1 /* bff */ : 2 /* tff */, maReplace);
    }

    vsapi->freeFrame(st);
    if (!samefields)
        vsapi->freeFrame(sb);

    return f;
}

static void VS_CC rffFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    rffData *d = (rffData *) instanceData;
    vsapi->freeNode(d->node);
    delete d;
}

void VS_CC rffCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    string msg;

    /* Allocate our private data. */
    unique_ptr<rffData> data(new rffData());

    /* Parse the D2V to get flags. */
    data->d2v.reset(d2vparse(vsapi->mapGetData(in, "d2v", 0, 0), msg));
    if (!data->d2v) {
        vsapi->mapSetError(out, msg.c_str());
        return;
    }

    /* Get our frame info and copy it, so we can modify it after. */
    data->node = vsapi->mapGetNode(in, "clip", 0, 0);
    data->vi   = *vsapi->getVideoInfo(data->node);

    /*
     * Parse all the RFF flags to figure out which fields go
     * with which frames, and out total number of frames after
     * apply the RFF flags.
     */
    for(int i = 0; i < data->vi.numFrames; i++) {
        frame f  = data->d2v->frames[i];
        bool rff = !!(data->d2v->gops[f.gop].flags[f.offset] & FRAME_FLAG_RFF);
        bool tff = !!(data->d2v->gops[f.gop].flags[f.offset] & FRAME_FLAG_TFF);
        bool progressive_frame = !!(data->d2v->gops[f.gop].flags[f.offset] & FRAME_FLAG_PROGRESSIVE);

        bool progressive_sequence = !!(data->d2v->gops[f.gop].info & GOP_FLAG_PROGRESSIVE_SEQUENCE);

        /*
         * In MPEG2 frame doubling and tripling happens only in progressive sequences.
         * H264 has no such thing, apparently, but frames still have to be progressive.
         */
        if (progressive_sequence ||
            (progressive_frame && data->d2v->mpeg_type == 264)) {
            /*
             * We repeat whole frames instead of fields, to turn one
             * coded progressive frame into either two or three
             * identical progressive frames.
             */
            rffField field;
            field.frame = i;
            field.type = Progressive;

            data->fields.push_back(field);
            data->fields.push_back(field);

            if (rff) {
                data->fields.push_back(field);
                data->fields.push_back(field);

                if (tff) {
                    data->fields.push_back(field);
                    data->fields.push_back(field);
                }
            }
        } else {
            /* Repeat fields. */

            rffField first_field, second_field;
            first_field.frame = second_field.frame = i;
            first_field.type = tff ? Top : Bottom;
            second_field.type = tff ? Bottom : Top;

            data->fields.push_back(first_field);
            data->fields.push_back(second_field);

            if (rff)
                data->fields.push_back(first_field);
        }
    }

    data->vi.numFrames = (int)data->fields.size() / 2;

    VSFilterDependency deps[] = {data->node, rpGeneral};
    vsapi->createVideoFilter(out, "applyrff", &data->vi, rffGetFrame, rffFree, fmParallel, deps, 1, data.get(), core);
    data.release();
}

}
