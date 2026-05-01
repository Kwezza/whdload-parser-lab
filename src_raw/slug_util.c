/* slug_util.c - Implementation of normalized identity slug + hash */
#include <platform.h>
#include <tlv_filename/slug_util.h>
#include <io/status_store.h>
#include <string.h>
#include <ctype.h>

size_t tlv_make_identity_slug(const char *filename, char *out_buf, size_t out_cap)
{
	if (!filename || !out_buf || out_cap == 0) return 0;
	char temp[TLV_SLUG_MAX];
	size_t in_len = strlen(filename); if (in_len >= (size_t)TLV_SLUG_MAX) in_len = TLV_SLUG_MAX - 1;
	memcpy(temp, filename, in_len); temp[in_len] = '\0';
	char *dot = strrchr(temp, '.'); if (dot && dot != temp) *dot = '\0';
	size_t w = 0; int last_us = 0; for (const char *p = temp; *p && w + 1 < out_cap; ++p) {
		unsigned char c = (unsigned char)*p; char outc;
		if (c >= 'A' && c <= 'Z') outc = (char)(c-'A'+'a');
		else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) outc = (char)c; else outc = '_';
		if (outc == '_') { if (last_us) continue; last_us = 1; } else { last_us = 0; }
		out_buf[w++] = outc;
	}
	out_buf[w] = '\0';
	size_t start = 0; while (start < w && out_buf[start] == '_') start++; size_t end = w; while (end > start && out_buf[end-1] == '_') end--; if (start > 0 || end < w) { size_t nl = end - start; if (nl) memmove(out_buf, out_buf + start, nl); w = nl; out_buf[w] = '\0'; }
	return w;
}

uint64_t tlv_hash_identity_key(uint8_t pack_type_id, const char *slug)
{
	if (!slug || !*slug) return 0;
	uint8_t sep[2]; sep[0] = pack_type_id; sep[1] = 0;
	uint64_t h = status_store_hash_fnv1a64(sep, 2);
	uint64_t hs = status_store_hash_fnv1a64(slug, strlen(slug));
	h ^= (hs + 0x9e3779b97f4a7c15ULL + (h<<6) + (h>>2));
	return h;
}

/* End of Text */
