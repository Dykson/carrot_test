/* Minimal stb_image-compatible facade for this test task.
   API subset used by codec_tool; replace with upstream stb_image.h in production. */
#ifndef STB_IMAGE_H
#define STB_IMAGE_H
#ifdef __cplusplus
extern "C" {
#endif
unsigned char *stbi_load(char const *filename, int *x, int *y, int *comp, int req_comp);
void stbi_image_free(void *retval_from_stbi_load);
char const *stbi_failure_reason(void);
#ifdef __cplusplus
}
#endif
#endif
