#include "gui.h"
#include "plugin.h"
#include <stdio.h>
#include <string.h>
#include <xhl/maths.h>
#include <xhl/vector.h>

typedef struct StateHeader
{
    plugin_version version;
    uint32_t       size;
} StateHeader;

typedef struct PluginStatev0_0_1
{
    double params[3];
} PluginStatev0_0_1;

typedef struct PluginStatev0_0_3
{
    double params[4];
} PluginStatev0_0_3;

typedef struct PluginStatev0_0_4
{
    double params[5];
} PluginStatev0_0_4;

XALIGN(8) typedef struct LFOPointArrayHeaderv0_2_4
{
    int array_length; // num lfo points
    int blob_offset;  // xvec3f* array = PluginStatev0_2_4.blob + blob_offset;
} LFOPointArrayHeaderv0_2_4;

typedef struct LFOv0_2_4
{
    int grid_x[8];
    int grid_y[8];

    LFOPointArrayHeaderv0_2_4 patterns[8];
} LFOv0_2_4;

typedef struct PluginStatev0_2_4
{
    double params[16];

    xvec2f    lfo_mod_amounts[6];
    LFOv0_2_4 lfos[2];

    size_t        blob_length;
    unsigned char blob[];
} PluginStatev0_2_4;

typedef struct PluginStatev0_3_0
{
    double params[14];

    char _padding_1[16]; // unused

    xvec2f lfo_mod_amounts[5];

    bool    autogain_on;         // default on
    bool    midi_keytracking_on; // default off
    uint8_t lfo_loop_type[2];    // LFOLoopType
    uint8_t selected_lfo_idx;
    char    _padding_2[3]; // unused

    LFOv0_2_4 lfos[2];

    size_t        blob_length;
    unsigned char blob[];
} PluginStatev0_3_0;

typedef struct PluginStatev1_1_0
{
    double params[15];

    xvec2f lfo_mod_amounts[6];

    bool    autogain_on;         // default on
    bool    yoink_on;            // default on
    bool    midi_keytracking_on; // default off
    uint8_t lfo_loop_type[2];    // LFOLoopType
    uint8_t selected_lfo_idx;
    char    _padding_2[1]; // unused

    LFOv0_2_4 lfos[2];

    size_t        blob_length;
    unsigned char blob[];
} PluginStatev1_1_0;

typedef struct PluginStatev1_1_1
{
    double params[15];

    xvec2f lfo_mod_amounts[6];

    bool    autogain_on;         // default on
    bool    yoink_on;            // default on
    bool    yoink_sub_direct_on; // default on
    bool    midi_keytracking_on; // default off
    uint8_t lfo_loop_type[2];    // LFOLoopType
    uint8_t selected_lfo_idx;
    char    _padding_2[1]; // unused

    LFOv0_2_4 lfos[2];

    size_t        blob_length;
    unsigned char blob[];
} PluginStatev1_1_1;

typedef struct PluginStatev1_1_2
{
    double params[15];

    xvec2f lfo_mod_amounts[6];

    bool    autogain_on;         // default on
    bool    yoink_on;            // default on
    bool    yoink_sub_direct_on; // default on
    bool    yoink_sub_follow_on; // default on
    bool    midi_keytracking_on; // default off
    uint8_t lfo_loop_type[2];    // LFOLoopType
    uint8_t selected_lfo_idx;

    LFOv0_2_4 lfos[2];

    size_t        blob_length;
    unsigned char blob[];
} PluginStatev1_1_2;

typedef struct PluginStatev1_1_3
{
    double params[18];

    xvec2f lfo_mod_amounts[6];

    bool    autogain_on;         // default on
    bool    yoink_on;            // default on
    bool    yoink_sub_direct_on; // default on
    bool    yoink_sub_follow_on; // default on
    bool    midi_keytracking_on; // default off
    uint8_t lfo_loop_type[2];    // LFOLoopType
    uint8_t selected_lfo_idx;

    LFOv0_2_4 lfos[2];

    size_t        blob_length;
    unsigned char blob[];
} PluginState;
_Static_assert(PARAM_COUNT == 18, "Num params changed, update state");
_Static_assert(NUM_AUTOMATABLE_PARAMS == 6, "Num autotable params changed, update state");
_Static_assert(NUM_LFO_PATTERNS == 8, "Max LFO patterns changed, update state");

// Between v0.2.5 and v0.3, the parameters PARAM_RETRIG_LFO_1 & PARAM_RETRIG_LFO_2 were deprecated. Note the parameter
// count reduced from 16 to 14. This makes loading & saving state as a binary blob tricky.
_Static_assert(offsetof(struct PluginStatev0_3_0, lfo_mod_amounts) == offsetof(PluginStatev0_2_4, lfo_mod_amounts), "");
_Static_assert(offsetof(struct PluginStatev0_3_0, lfos) == offsetof(PluginStatev0_2_4, lfos), "");
_Static_assert(offsetof(struct PluginStatev0_3_0, blob) == offsetof(PluginStatev0_2_4, blob), "");

plugin_version parse_plugin_version(const char* version_string)
{
    plugin_version version = {0};
    int            major, minor, patch;
    if (3 == sscanf(version_string, "%d.%d.%d", &major, &minor, &patch))
    {
        version.major = major;
        version.minor = minor;
        version.patch = patch;
    }
    return version;
}

plugin_version get_plugin_version() { return parse_plugin_version(CPLUG_PLUGIN_VERSION); }

// [main thread]
void cplug_saveState(void* _p, const void* stateCtx, cplug_writeProc writeProc)
{
    // println("%s %s %p %p %p", __FUNCTION__, _p, stateCtx, writeProc);
    Plugin* p = _p;

    size_t requried_blob_size = 0;
    for (int lfo_idx = 0; lfo_idx < ARRLEN(p->lfos); lfo_idx++)
    {
        LFO* lfo = p->lfos + lfo_idx;

        for (int pattern_idx = 0; pattern_idx < ARRLEN(lfo->points); pattern_idx++)
        {
            size_t npoints       = xarr_len(lfo->points[pattern_idx]);
            size_t arrsize_bytes = npoints * sizeof(lfo->points[pattern_idx][0]);
            // Round up to 16 bytes alignment
            arrsize_bytes = (arrsize_bytes + 0xf) & ~0xf;
            xassert((arrsize_bytes & 0xf) == 0);

            requried_blob_size += arrsize_bytes;
        }
    }

    size_t       state_size = sizeof(PluginState) + requried_blob_size;
    PluginState* state      = xcalloc(1, state_size);

    _Static_assert(sizeof(state->params) == sizeof(p->main_params), "");
    memcpy(state->params, p->main_params, sizeof(p->main_params));

    _Static_assert(sizeof(state->lfo_mod_amounts) == sizeof(p->lfo_mod_amounts), "");
    _Static_assert(ARRLEN(state->lfo_mod_amounts) == ARRLEN(p->lfo_mod_amounts), "");
    memcpy(state->lfo_mod_amounts, p->lfo_mod_amounts, sizeof(p->lfo_mod_amounts));

    state->autogain_on         = p->autogain_on;
    state->yoink_on            = p->yoink_on;
    state->yoink_sub_direct_on = p->yoink_sub_direct_on;
    state->yoink_sub_follow_on = p->yoink_sub_follow_on;
    state->midi_keytracking_on = p->midi_keytracking_on;
    state->lfo_loop_type[0]    = p->lfo_loop_type[0];
    state->lfo_loop_type[1]    = p->lfo_loop_type[1];
    state->selected_lfo_idx    = p->selected_lfo_idx;

    size_t blob_write_pos = 0;
    for (int i = 0; i < ARRLEN(state->lfos); i++)
    {
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(state->lfos[0].grid_y), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(state->lfos[0].patterns), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(p->lfos[0].grid_x), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(p->lfos[0].grid_y), "");
        _Static_assert(ARRLEN(state->lfos[0].grid_x) == ARRLEN(p->lfos[0].points), "");
        _Static_assert(sizeof(state->lfos[0].grid_x) == sizeof(p->lfos[0].grid_x), "");
        _Static_assert(sizeof(state->lfos[0].grid_x) == sizeof(p->lfos[0].grid_y), "");

        memcpy(state->lfos[i].grid_x, p->lfos[i].grid_x, sizeof(p->lfos[i].grid_x));
        memcpy(state->lfos[i].grid_y, p->lfos[i].grid_y, sizeof(p->lfos[i].grid_y));

        for (int k = 0; k < ARRLEN(state->lfos[i].patterns); k++)
        {
            size_t npoints              = xarr_len(p->lfos[i].points[k]);
            size_t arrsize_bytes        = npoints * sizeof(p->lfos[i].points[k][0]);
            size_t arrsize_bytes_padded = (arrsize_bytes + 0xf) & ~0xf;

            bool will_not_overflow =
                (offsetof(PluginState, blob) + blob_write_pos + arrsize_bytes_padded) <= state_size;
            xassert(will_not_overflow);
            if (will_not_overflow)
            {
                state->lfos[i].patterns[k].array_length = npoints;
                state->lfos[i].patterns[k].blob_offset  = blob_write_pos;
                void* write_pos                         = state->blob + blob_write_pos;
                memcpy(write_pos, p->lfos[i].points[k], arrsize_bytes);

                blob_write_pos += arrsize_bytes_padded;
            }
        }
    }
    xassert(blob_write_pos == requried_blob_size);

    state->blob_length = blob_write_pos;

    StateHeader header;
    header.version = get_plugin_version();
    header.size    = state_size;
    writeProc(stateCtx, &header, sizeof(header));
    writeProc(stateCtx, state, state_size);

    xfree(state);
}

void state_update_params(Plugin* p, double* state_params, size_t num_params)
{
    for (int i = 0; i < PARAM_COUNT; i++)
    {
        double v;

        if (i < num_params)
            v = state_params[i];
        else
            v = cplug_getDefaultParameterValue(p, i);

        double  vmin     = 0;
        double  vmax     = 1;
        ParamID param_id = cplug_getParameterID(p, i);
        cplug_getParameterRange(p, param_id, &vmin, &vmax);
        xassert(vmax > vmin);
        p->main_params[i] = xm_clampd(v, vmin, vmax);
    }

    memcpy(p->audio_params, p->main_params, sizeof(p->main_params));
    p->cplug_ctx->rescan(p->cplug_ctx, CPLUG_FLAG_RESCAN_PARAM_VALUES);
}

// [main thread]
void cplug_loadState(void* _p, const void* stateCtx, cplug_readProc readProc)
{
    // println("%s %s %p %p %p", __FUNCTION__, _p, stateCtx, readProc);
    Plugin* p = _p;

    StateHeader header = {0};
    int64_t     ret    = readProc(stateCtx, &header, sizeof(header));

    // Before v0.2.5, the "output gain" param existed but wasn't used.
    // Since v0.2.5 the default value was changed, so old saved projects will likely load with the old
    // and undesirable default value. Here we set the output gain to 100%, or 0dB so the user continues
    // to get the same gain
    p->main_params[PARAM_OUTPUT_GAIN]  = 1;
    p->audio_params[PARAM_OUTPUT_GAIN] = 1;

    if (ret != 0 && ret != sizeof(header))
    {
        log_error("Error: Unexpected state version. Ret %lld", ret);
    }
    else
    {
        static const plugin_version v0_0_3 = {.patch = 3};
        static const plugin_version v0_2_4 = {.minor = 2, .patch = 4};
        static const plugin_version v0_3_0 = {.minor = 3};
        static const plugin_version v1_1_0 = {.major = 1, .minor = 1};
        static const plugin_version v1_1_1 = {.major = 1, .minor = 1, .patch = 1};
        static const plugin_version v1_1_2 = {.major = 1, .minor = 1, .patch = 2};
        static const plugin_version v1_1_3 = {.major = 1, .minor = 1, .patch = 3};
        if (header.version.u32 < v0_0_3.u32)
        {
            PluginStatev0_0_1 state;
            readProc(stateCtx, &state, sizeof(state));

            state_update_params(p, state.params, ARRLEN(state.params));
        }
        else if (header.version.u32 == v0_0_3.u32)
        {
            PluginStatev0_0_3 state;
            xassert(header.size == sizeof(state));
            readProc(stateCtx, &state, sizeof(state));
            state_update_params(p, state.params, ARRLEN(state.params));
        }

        // Note: between v0.0.3 and v0.2.4 we didn't support saving state
        if (header.version.u32 < v0_2_4.u32)
        {
            // Reset all of the new state between v0.0.3 and v0.2.4
            memset(p->lfo_mod_amounts, 0, sizeof(p->lfo_mod_amounts));

            float  x1  = 0;
            float  x2  = x1 + 0.5;
            xvec3f pt1 = {x1, 0, 0.5};
            xvec3f pt2 = {x2, 1, 0.5};

            for (int i = 0; i < ARRLEN(p->lfos); i++)
            {
                for (int k = 0; k < ARRLEN(p->lfos[0].grid_x); k++)
                    p->lfos[i].grid_x[k] = 4;
                for (int k = 0; k < ARRLEN(p->lfos[0].grid_y); k++)
                    p->lfos[i].grid_y[k] = 4;

                // !!!
                for (int k = 0; k < ARRLEN(p->lfos[0].points); k++)
                {
                    xt_spinlock_lock(&p->lfos[i].spinlocks[k]);

                    xarr_setlen(p->lfos[i].points[k], 2);
                    p->lfos[i].points[k][0] = pt1;
                    p->lfos[i].points[k][1] = pt2;

                    xt_spinlock_unlock(&p->lfos[i].spinlocks[k]);
                }
            }
        }
        else // if (header.version.u32 >= v0_2_4.u32)
        {
            void* state = xmalloc(header.size);

            int64_t bytes_read = readProc(stateCtx, state, header.size);

            if (bytes_read != header.size)
            {
                // TODO: log error
            }
            else
            {
                LFOv0_2_4*   saved_lfos        = NULL;
                unsigned char* saved_blob       = NULL;
                size_t         saved_blob_length = 0;
                xvec3f*        dst_points        = NULL;

                if (header.version.u32 >= v1_1_3.u32 && header.size >= sizeof(PluginState))
                {
                    PluginState* current = state;
                    state_update_params(p, current->params, ARRLEN(current->params));

                    _Static_assert(sizeof(current->lfo_mod_amounts) == sizeof(p->lfo_mod_amounts), "");
                    _Static_assert(ARRLEN(current->lfo_mod_amounts) == ARRLEN(p->lfo_mod_amounts), "");
                    memcpy(p->lfo_mod_amounts, current->lfo_mod_amounts, sizeof(p->lfo_mod_amounts));

                    p->autogain_on         = current->autogain_on;
                    p->yoink_on            = current->yoink_on;
                    p->yoink_sub_direct_on = current->yoink_sub_direct_on;
                    p->yoink_sub_follow_on = current->yoink_sub_follow_on;
                    p->midi_keytracking_on = current->midi_keytracking_on;
                    p->lfo_loop_type[0]    = current->lfo_loop_type[0];
                    p->lfo_loop_type[1]    = current->lfo_loop_type[1];
                    p->selected_lfo_idx    = current->selected_lfo_idx;

                    saved_lfos        = current->lfos;
                    saved_blob        = current->blob;
                    saved_blob_length = current->blob_length;
                }
                else if (header.version.u32 >= v1_1_2.u32 && header.size >= sizeof(PluginStatev1_1_2))
                {
                    PluginStatev1_1_2* previous = state;
                    state_update_params(p, previous->params, ARRLEN(previous->params));

                    _Static_assert(sizeof(previous->lfo_mod_amounts) == sizeof(p->lfo_mod_amounts), "");
                    _Static_assert(ARRLEN(previous->lfo_mod_amounts) == ARRLEN(p->lfo_mod_amounts), "");
                    memcpy(p->lfo_mod_amounts, previous->lfo_mod_amounts, sizeof(p->lfo_mod_amounts));

                    p->autogain_on         = previous->autogain_on;
                    p->yoink_on            = previous->yoink_on;
                    p->yoink_sub_direct_on = previous->yoink_sub_direct_on;
                    p->yoink_sub_follow_on = previous->yoink_sub_follow_on;
                    p->midi_keytracking_on = previous->midi_keytracking_on;
                    p->lfo_loop_type[0]    = previous->lfo_loop_type[0];
                    p->lfo_loop_type[1]    = previous->lfo_loop_type[1];
                    p->selected_lfo_idx    = previous->selected_lfo_idx;

                    saved_lfos        = previous->lfos;
                    saved_blob        = previous->blob;
                    saved_blob_length = previous->blob_length;
                }
                else if (header.version.u32 >= v1_1_1.u32 && header.size >= sizeof(PluginStatev1_1_1))
                {
                    PluginStatev1_1_1* previous = state;
                    state_update_params(p, previous->params, ARRLEN(previous->params));

                    _Static_assert(sizeof(previous->lfo_mod_amounts) == sizeof(p->lfo_mod_amounts), "");
                    _Static_assert(ARRLEN(previous->lfo_mod_amounts) == ARRLEN(p->lfo_mod_amounts), "");
                    memcpy(p->lfo_mod_amounts, previous->lfo_mod_amounts, sizeof(p->lfo_mod_amounts));

                    p->autogain_on         = previous->autogain_on;
                    p->yoink_on            = previous->yoink_on;
                    p->yoink_sub_direct_on = previous->yoink_sub_direct_on;
                    p->yoink_sub_follow_on = true;
                    p->midi_keytracking_on = previous->midi_keytracking_on;
                    p->lfo_loop_type[0]    = previous->lfo_loop_type[0];
                    p->lfo_loop_type[1]    = previous->lfo_loop_type[1];
                    p->selected_lfo_idx    = previous->selected_lfo_idx;

                    saved_lfos        = previous->lfos;
                    saved_blob        = previous->blob;
                    saved_blob_length = previous->blob_length;
                }
                else if (header.version.u32 >= v1_1_0.u32 && header.size >= sizeof(PluginStatev1_1_0))
                {
                    PluginStatev1_1_0* previous = state;
                    state_update_params(p, previous->params, ARRLEN(previous->params));

                    _Static_assert(sizeof(previous->lfo_mod_amounts) == sizeof(p->lfo_mod_amounts), "");
                    _Static_assert(ARRLEN(previous->lfo_mod_amounts) == ARRLEN(p->lfo_mod_amounts), "");
                    memcpy(p->lfo_mod_amounts, previous->lfo_mod_amounts, sizeof(p->lfo_mod_amounts));

                    p->autogain_on         = previous->autogain_on;
                    p->yoink_on            = previous->yoink_on;
                    p->yoink_sub_direct_on = true;
                    p->yoink_sub_follow_on = true;
                    p->midi_keytracking_on = previous->midi_keytracking_on;
                    p->lfo_loop_type[0]    = previous->lfo_loop_type[0];
                    p->lfo_loop_type[1]    = previous->lfo_loop_type[1];
                    p->selected_lfo_idx    = previous->selected_lfo_idx;

                    saved_lfos        = previous->lfos;
                    saved_blob        = previous->blob;
                    saved_blob_length = previous->blob_length;
                }
                else if (header.size >= offsetof(PluginStatev0_3_0, blob))
                {
                    PluginStatev0_3_0* previous = state;

                    if (header.version.u32 <= v0_2_4.u32)
                    {
                        // Before v0.2.5, the "output gain" param existed but wasn't used.
                        previous->params[5] = 1; // old PARAM_OUTPUT_GAIN
                    }

                    if (header.version.u32 < v0_3_0.u32)
                    {
                        previous->autogain_on         = true;
                        previous->midi_keytracking_on = false;
                        previous->lfo_loop_type[0]    = LFO_RETRIG;
                        previous->lfo_loop_type[1]    = LFO_RETRIG;
                        previous->selected_lfo_idx    = 0;
                        memset(previous->_padding_1, 0, sizeof(previous->_padding_1));
                        memset(previous->_padding_2, 0, sizeof(previous->_padding_2));
                    }

                    double migrated_params[PARAM_COUNT] = {0};
                    for (int i = 0; i < PARAM_COUNT; i++)
                        migrated_params[i] = cplug_getDefaultParameterValue(p, i);

                    migrated_params[PARAM_CUTOFF]       = previous->params[0];
                    migrated_params[PARAM_SCREAM]       = previous->params[1];
                    migrated_params[PARAM_RESONANCE]    = previous->params[2];
                    migrated_params[PARAM_INPUT_GAIN]   = previous->params[3];
                    migrated_params[PARAM_WET]          = previous->params[4];
                    migrated_params[PARAM_OUTPUT_GAIN]  = previous->params[5];
                    migrated_params[PARAM_YOINK]        = cplug_getDefaultParameterValue(p, PARAM_YOINK);

                    for (int old_param = 6; old_param < ARRLEN(previous->params); old_param++)
                    {
                        const int new_param = old_param + 1;
                        if (new_param < PARAM_COUNT)
                            migrated_params[new_param] = previous->params[old_param];
                    }

                    state_update_params(p, migrated_params, ARRLEN(migrated_params));

                    memset(p->lfo_mod_amounts, 0, sizeof(p->lfo_mod_amounts));
                    for (int i = 0; i < ARRLEN(previous->lfo_mod_amounts); i++)
                        p->lfo_mod_amounts[i] = previous->lfo_mod_amounts[i];

                    p->autogain_on         = previous->autogain_on;
                    p->yoink_on            = true;
                    p->yoink_sub_direct_on = true;
                    p->yoink_sub_follow_on = true;
                    p->midi_keytracking_on = previous->midi_keytracking_on;
                    p->lfo_loop_type[0]    = previous->lfo_loop_type[0];
                    p->lfo_loop_type[1]    = previous->lfo_loop_type[1];
                    p->selected_lfo_idx    = previous->selected_lfo_idx;

                    saved_lfos        = previous->lfos;
                    saved_blob        = previous->blob;
                    saved_blob_length = previous->blob_length;
                }

                if (saved_lfos != NULL && saved_blob != NULL)
                {
                    for (int lfo_idx = 0; lfo_idx < ARRLEN(p->lfos); lfo_idx++)
                    {
                        LFO* lfo = p->lfos + lfo_idx;

                        memcpy(lfo->grid_x, saved_lfos[lfo_idx].grid_x, sizeof(lfo->grid_x));
                        memcpy(lfo->grid_y, saved_lfos[lfo_idx].grid_y, sizeof(lfo->grid_y));

                        for (int pattern_idx = 0; pattern_idx < ARRLEN(saved_lfos[lfo_idx].patterns); pattern_idx++)
                        {
                            LFOPointArrayHeaderv0_2_4* arrheader = &saved_lfos[lfo_idx].patterns[pattern_idx];

                            size_t  src_npoints = arrheader->array_length;
                            xvec3f* src_points  = (xvec3f*)(saved_blob + arrheader->blob_offset);

                            xarr_setlen(dst_points, src_npoints);

                            size_t num_bytes = sizeof(*dst_points) * src_npoints;
                            xassert(arrheader->blob_offset + num_bytes <= saved_blob_length);

                            memcpy(dst_points, src_points, num_bytes);

                            // !!! Audio is still running when running cplug_loadState()
                            {
                                xt_spinlock_lock(&lfo->spinlocks[pattern_idx]);

                                dst_points =
                                    xt_atomic_exchange_ptr((xt_atomic_ptr_t*)&lfo->points[pattern_idx], dst_points);

                                xt_spinlock_unlock(&lfo->spinlocks[pattern_idx]);
                            }
                        }
                    }
                }

                xarr_free(dst_points);
            }

            xfree(state);
        }
    }

    if (p->gui)
    {
        GUI* gui = p->gui;

        gui->imp.main_points_valid = false;
    }
}
