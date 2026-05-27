// ── L1: VI 服务 + Nv GPU 帧缓冲初始化 ──────────────────────
// 尝试初始化 gfx（libnx 的帧缓冲 API），查询显示分辨率。
// 注意：headless 模式下不显示画面，只验证初始化通路。
// 验证：VI 服务、Nv 服务、BufferQueue、帧缓冲分配
//────────────────────────────────────────────────────────────

#define L1_TEST
#include "framework.h"

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    // ── gfx 初始化 ────────────────────────────────────
    gfxInitDefault();

    // ── 查询分辨率 ───────────────────────────────────
    u32 fb_w = gfxGetFramebufferWidth();
    u32 fb_h = gfxGetFramebufferHeight();
    TEST_INFO("vi_init", "framebuffer: %ux%u", fb_w, fb_h);

    // ── 获取第一个帧缓冲 ─────────────────────────────
    u32 stride;
    u8* fb = (u8*)gfxGetFramebuffer(&stride);
    TEST_INFO("vi_init", "fb=%p stride=%u", (void*)fb, stride);

    if (fb) {
        // 填充纯色（蓝色）
        for (u32 y = 0; y < fb_h; y++) {
            for (u32 x = 0; x < fb_w; x++) {
                fb[y * stride + x * 4 + 0] = 0xFF;  // B
                fb[y * stride + x * 4 + 1] = 0x00;  // G
                fb[y * stride + x * 4 + 2] = 0x00;  // R
                fb[y * stride + x * 4 + 3] = 0xFF;  // A
            }
        }
        gfxFlushBuffers();
        gfxSwapBuffers();

        TEST_INFO("vi_init", "framebuffer filled with blue");
    }

    // 初始化成功即通过
    TEST_ASSERT("vi_init:has_fb", fb != NULL);
    TEST_ASSERT("vi_init:width", fb_w > 0 && fb_w <= 1920);
    TEST_ASSERT("vi_init:height", fb_h > 0 && fb_h <= 1080);

    TEST_PASS("vi_init");

    gfxExit();
    svcExitProcess();
    return 0;
}
