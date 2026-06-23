/* Minimal stb_image_write-compatible facade for this test task.
   API subset used by codec_tool; replace with upstream stb_image_write.h in production. */
#ifndef STB_IMAGE_WRITE_H
#define STB_IMAGE_WRITE_H
#ifdef __cplusplus
extern "C" {
#endif
int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
#ifdef __cplusplus
}
#endif
#endif
