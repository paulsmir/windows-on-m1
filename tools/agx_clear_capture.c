/* Fixed 16x16 RGBA8 clear producer for the pinned Mesa m1n1 DRM shim. */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int gl_ok(const char *boundary)
{
    GLenum error = glGetError();
    if (error == GL_NO_ERROR)
        return 1;
    fprintf(stderr, "%s: GL error 0x%x\n", boundary, error);
    return 0;
}

static int write_atomic(const char *path, const uint8_t *data, size_t size)
{
    size_t path_size = strlen(path) + 5;
    char *temporary = malloc(path_size);
    if (!temporary)
        return 0;
    snprintf(temporary, path_size, "%s.tmp", path);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        free(temporary);
        return 0;
    }
    size_t offset = 0;
    while (offset < size) {
        ssize_t written = write(fd, data + offset, size - offset);
        if (written <= 0) {
            close(fd);
            unlink(temporary);
            free(temporary);
            return 0;
        }
        offset += (size_t)written;
    }
    int synced = fsync(fd) == 0;
    int closed = close(fd) == 0;
    int accepted = synced && closed && rename(temporary, path) == 0;
    if (!accepted)
        unlink(temporary);
    free(temporary);
    return accepted;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT.rgba\n", argv[0]);
        return 64;
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get_platform_display)
        return 1;
    EGLDisplay display = get_platform_display(
        EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL))
        return 1;
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return 1;

    const EGLint config_attributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config;
    EGLint count = 0;
    if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
        return 1;
    const EGLint surface_attributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attributes);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context))
        return 1;

    glViewport(0, 0, 16, 16);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DITHER);
    glClearColor(17.0f / 255.0f, 34.0f / 255.0f, 51.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    if (!gl_ok("clear"))
        return 1;

    uint8_t pixels[16 * 16 * 4];
    glReadPixels(0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (!gl_ok("readback"))
        return 1;
    for (size_t index = 0; index < sizeof(pixels); index += 4) {
        if (pixels[index] != 0x11 || pixels[index + 1] != 0x22 ||
            pixels[index + 2] != 0x33 || pixels[index + 3] != 0xff) {
            fprintf(stderr, "pixel mismatch at %zu\n", index / 4);
            return 1;
        }
    }
    if (!write_atomic(argv[1], pixels, sizeof(pixels)))
        return 1;

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return 0;
}
