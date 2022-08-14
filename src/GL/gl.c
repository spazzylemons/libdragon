#include "GL/gl.h"
#include "rdpq.h"
#include "rspq.h"
#include "display.h"
#include "rdp.h"
#include "utils.h"
#include "gl_internal.h"
#include <string.h>
#include <math.h>

gl_state_t state;

#define assert_framebuffer() ({ \
    assertf(state.cur_framebuffer != NULL, "GL: No target is set!"); \
})

uint32_t gl_get_type_size(GLenum type)
{
    switch (type) {
    case GL_BYTE:
        return sizeof(GLbyte);
    case GL_UNSIGNED_BYTE:
        return sizeof(GLubyte);
    case GL_SHORT:
        return sizeof(GLshort);
    case GL_UNSIGNED_SHORT:
        return sizeof(GLushort);
    case GL_INT:
        return sizeof(GLint);
    case GL_UNSIGNED_INT:
        return sizeof(GLuint);
    case GL_FLOAT:
        return sizeof(GLfloat);
    case GL_DOUBLE:
        return sizeof(GLdouble);
    default:
        return 0;
    }
}

void gl_set_framebuffer(gl_framebuffer_t *framebuffer)
{
    state.cur_framebuffer = framebuffer;
    rdpq_set_color_image(state.cur_framebuffer->color_buffer);
    rdpq_set_z_image_raw(0, PhysicalAddr(state.cur_framebuffer->depth_buffer));
}

void gl_set_default_framebuffer()
{
    surface_t *ctx;

    RSP_WAIT_LOOP(200) {
        if ((ctx = display_lock())) {
            break;
        }
    }

    gl_framebuffer_t *fb = &state.default_framebuffer;

    if (fb->depth_buffer != NULL && (fb->color_buffer == NULL 
                                    || fb->color_buffer->width != ctx->width 
                                    || fb->color_buffer->height != ctx->height)) {
        free_uncached(fb->depth_buffer);
        fb->depth_buffer = NULL;
    }

    fb->color_buffer = ctx;

    // TODO: only allocate depth buffer if depth test is enabled? Lazily allocate?
    if (fb->depth_buffer == NULL) {
        // TODO: allocate in separate RDRAM bank?
        fb->depth_buffer = malloc_uncached_aligned(64, ctx->width * ctx->height * 2);
    }

    gl_set_framebuffer(fb);
}

void gl_init()
{
    rdpq_init();

    memset(&state, 0, sizeof(state));

    gl_matrix_init();
    gl_lighting_init();
    gl_texture_init();
    gl_rendermode_init();
    gl_array_init();
    gl_primitive_init();
    gl_pixel_init();
    gl_list_init();
    gl_buffer_init();

    glDrawBuffer(GL_FRONT);
    glDepthRange(0, 1);
    glClearDepth(1.0);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    rdpq_set_other_modes_raw(0);
    gl_set_default_framebuffer();
    glViewport(0, 0, state.default_framebuffer.color_buffer->width, state.default_framebuffer.color_buffer->height);
}

void gl_close()
{
    gl_buffer_close();
    gl_list_close();
    gl_primitive_close();
    gl_texture_close();
    rdpq_close();
}

void gl_swap_buffers()
{
    rdpq_sync_full((void(*)(void*))display_show, state.default_framebuffer.color_buffer);
    rspq_flush();
    gl_set_default_framebuffer();
}

GLenum glGetError(void)
{
    GLenum error = state.current_error;
    state.current_error = GL_NO_ERROR;
    return error;
}

void gl_set_error(GLenum error)
{
    state.current_error = error;
    assert(error);
}

void gl_set_flag(GLenum target, bool value)
{
    switch (target) {
    case GL_SCISSOR_TEST:
        state.is_scissor_dirty = value != state.scissor_test;
        state.scissor_test = value;
        break;
    case GL_CULL_FACE:
        state.cull_face = value;
        break;
    case GL_DEPTH_TEST:
        GL_SET_STATE(state.depth_test, value, state.is_rendermode_dirty);
        break;
    case GL_TEXTURE_1D:
        GL_SET_STATE(state.texture_1d, value, state.is_rendermode_dirty);
        break;
    case GL_TEXTURE_2D:
        GL_SET_STATE(state.texture_2d, value, state.is_rendermode_dirty);
        break;
    case GL_BLEND:
        GL_SET_STATE(state.blend, value, state.is_rendermode_dirty);
        break;
    case GL_ALPHA_TEST:
        GL_SET_STATE(state.alpha_test, value, state.is_rendermode_dirty);
        break;
    case GL_DITHER:
        GL_SET_STATE(state.dither, value, state.is_rendermode_dirty);
        break;
    case GL_FOG:
        GL_SET_STATE(state.fog, value, state.is_rendermode_dirty);
    case GL_LIGHTING:
        state.lighting = value;
        break;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
    case GL_LIGHT4:
    case GL_LIGHT5:
    case GL_LIGHT6:
    case GL_LIGHT7:
        state.lights[target - GL_LIGHT0].enabled = value;
        break;
    case GL_COLOR_MATERIAL:
        state.color_material = value;
        break;
    case GL_MULTISAMPLE_ARB:
        GL_SET_STATE(state.multisample, value, state.is_rendermode_dirty);
        break;
    case GL_TEXTURE_GEN_S:
        state.s_gen.enabled = value;
        break;
    case GL_TEXTURE_GEN_T:
        state.t_gen.enabled = value;
        break;
    case GL_TEXTURE_GEN_R:
        state.r_gen.enabled = value;
        break;
    case GL_TEXTURE_GEN_Q:
        state.q_gen.enabled = value;
        break;
    case GL_NORMALIZE:
        state.normalize = value;
        break;
    case GL_CLIP_PLANE0:
    case GL_CLIP_PLANE1:
    case GL_CLIP_PLANE2:
    case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4:
    case GL_CLIP_PLANE5:
        assertf(!value, "User clip planes are not supported!");
        break;
    case GL_STENCIL_TEST:
        assertf(!value, "Stencil test is not supported!");
        break;
    case GL_COLOR_LOGIC_OP:
    case GL_INDEX_LOGIC_OP:
        assertf(!value, "Logical pixel operation is not supported!");
        break;
    case GL_POINT_SMOOTH:
    case GL_LINE_SMOOTH:
    case GL_POLYGON_SMOOTH:
        assertf(!value, "Smooth rendering is not supported (Use multisampling instead)!");
        break;
    case GL_LINE_STIPPLE:
    case GL_POLYGON_STIPPLE:
        assertf(!value, "Stipple is not supported!");
        break;
    case GL_POLYGON_OFFSET_FILL:
    case GL_POLYGON_OFFSET_LINE:
    case GL_POLYGON_OFFSET_POINT:
        assertf(!value, "Polygon offset is not supported!");
        break;
    case GL_SAMPLE_ALPHA_TO_COVERAGE_ARB:
    case GL_SAMPLE_ALPHA_TO_ONE_ARB:
    case GL_SAMPLE_COVERAGE_ARB:
        assertf(!value, "Coverage value manipulation is not supported!");
        break;
    case GL_MAP1_COLOR_4:
    case GL_MAP1_INDEX:
    case GL_MAP1_NORMAL:
    case GL_MAP1_TEXTURE_COORD_1:
    case GL_MAP1_TEXTURE_COORD_2:
    case GL_MAP1_TEXTURE_COORD_3:
    case GL_MAP1_TEXTURE_COORD_4:
    case GL_MAP1_VERTEX_3:
    case GL_MAP1_VERTEX_4:
    case GL_MAP2_COLOR_4:
    case GL_MAP2_INDEX:
    case GL_MAP2_NORMAL:
    case GL_MAP2_TEXTURE_COORD_1:
    case GL_MAP2_TEXTURE_COORD_2:
    case GL_MAP2_TEXTURE_COORD_3:
    case GL_MAP2_TEXTURE_COORD_4:
    case GL_MAP2_VERTEX_3:
    case GL_MAP2_VERTEX_4:
        assertf(!value, "Evaluators are not supported!");
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glEnable(GLenum target)
{
    gl_set_flag(target, true);
}

void glDisable(GLenum target)
{
    gl_set_flag(target, false);
}

void glDrawBuffer(GLenum buf)
{
    switch (buf) {
    case GL_NONE:
    case GL_FRONT_LEFT:
    case GL_FRONT:
    case GL_LEFT:
    case GL_FRONT_AND_BACK:
        state.draw_buffer = buf;
        break;
    case GL_FRONT_RIGHT:
    case GL_BACK_LEFT:
    case GL_BACK_RIGHT:
    case GL_BACK:
    case GL_RIGHT:
    case GL_AUX0:
    case GL_AUX1:
    case GL_AUX2:
    case GL_AUX3:
        gl_set_error(GL_INVALID_OPERATION);
        return;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glClear(GLbitfield buf)
{
    assert_framebuffer();

    rdpq_set_other_modes_raw(SOM_CYCLE_FILL);
    state.is_rendermode_dirty = true;

    gl_update_scissor();

    gl_framebuffer_t *fb = state.cur_framebuffer;

    if (buf & (GL_STENCIL_BUFFER_BIT | GL_ACCUM_BUFFER_BIT)) {
        assertf(0, "Only color and depth buffers are supported!");
    }

    if (buf & GL_DEPTH_BUFFER_BIT) {
        uint32_t old_cfg = rdpq_config_disable(RDPQ_CFG_AUTOSCISSOR);

        rdpq_set_color_image_raw(0, PhysicalAddr(fb->depth_buffer), FMT_RGBA16, fb->color_buffer->width, fb->color_buffer->height, fb->color_buffer->width * 2);
        rdpq_set_fill_color(color_from_packed16(state.clear_depth * 0xFFFC));
        rdpq_fill_rectangle(0, 0, fb->color_buffer->width, fb->color_buffer->height);

        rdpq_set_color_image(fb->color_buffer);

        rdpq_config_set(old_cfg);
    }

    if (buf & GL_COLOR_BUFFER_BIT) {
        rdpq_set_fill_color(RGBA32(
            CLAMPF_TO_U8(state.clear_color[0]), 
            CLAMPF_TO_U8(state.clear_color[1]), 
            CLAMPF_TO_U8(state.clear_color[2]), 
            CLAMPF_TO_U8(state.clear_color[3])));
        rdpq_fill_rectangle(0, 0, fb->color_buffer->width, fb->color_buffer->height);
    }
}

void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    state.clear_color[0] = r;
    state.clear_color[1] = g;
    state.clear_color[2] = b;
    state.clear_color[3] = a;
}

void glClearDepth(GLclampd d)
{
    state.clear_depth = d;
}

void glRenderMode(GLenum mode)
{
    switch (mode) {
    case GL_RENDER:
        break;
    case GL_SELECT:
    case GL_FEEDBACK:
        assertf(0, "Select and feedback modes are not supported!");
        break;
    default:
        gl_set_error(GL_INVALID_ENUM);
        return;
    }
}

void glFlush(void)
{
    rspq_flush();
}

void glFinish(void)
{
    rspq_wait();
}
