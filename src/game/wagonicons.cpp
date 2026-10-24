// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 THIONG
// Part of Imperivm Resurrected. GPL-3.0-or-later license with an exception for Steamworks: see LICENSE and ADDITIONAL-PERMISSION.md.
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "game/wagonicons.h"
#include "core/config.h"
#include "core/log.h"
#include "hooks/gamefiles.h"

#include "addr/script.h"
#include "addr/state.h"
#include "addr/world.h"

namespace {
typedef char *(__fastcall *IsKindOf_t)(char *object, void *unused, const char *className);
typedef void(__cdecl *ObjScreenRect_t)(char *obj, int *rect);
typedef unsigned(__fastcall *VisionRun_t)(void *query, void *edx, int *point, void *player);

const unsigned kHandleCount = 0x10000;
const unsigned kObjFlags = 0x2c, kObjPosX = 0x24, kObjPosY = 0x28;
// Wagon::amount and Wagon::restype read these two fields; restype is 0 for food and 1 for gold
const unsigned kWagonAmount = 0x1cc, kWagonResType = 0x1d4;
const unsigned kHiddenFlag = 0x200000;
const unsigned kOffPlayer = 0x12c8, kOffVision = 0x44d4, kVisionFogOn = 0xd4;
const unsigned kVisibleViewRect = 0x618;
const int kFoodType = 0, kGoldType = 1;
const DWORD kScanMs = 500, kStaleMs = 300;
const int kGap = 2;
const uint32_t kOutline = 0xff141008u;
const unsigned kReadMode = 1;
const char *const kIconPaths[2] = {"ui/infobar/common/food_cost.bmp", "ui/infobar/common/gold_cost.bmp"};

struct Icon {
	int w = 0, h = 0;
	std::vector<uint32_t> px;
};
Icon g_icons[2];
bool g_iconsTried = false;

struct Badge {
	int x, y;
	int kind;
};

struct Shown {
	char *obj;
	unsigned short handle;
};

std::mutex g_mx;
std::vector<Badge> g_badges;
DWORD g_badgesTick = 0;

std::vector<Shown> g_wagons;
DWORD g_scanTick = 0;

template <typename T>
T At(const void *p, unsigned off) { return *reinterpret_cast<const T *>(static_cast<const char *>(p) + off); }

char **Handles() { return reinterpret_cast<char **>(addr::kHandleTable); }

char *WagonOf(char *obj)
{
	if (!obj || (reinterpret_cast<uintptr_t>(obj) & 3)) return nullptr;
	__try {
		char *bases = *reinterpret_cast<char **>(obj + 4);
		if (!bases) return nullptr;
		const int offset = *reinterpret_cast<int *>(bases + 4);
		if (offset < 0 || offset > 0x10000) return nullptr;
		char *base = obj + offset + 4;
		void **vtable = *reinterpret_cast<void ***>(base);
		if (!vtable) return nullptr;
		return reinterpret_cast<IsKindOf_t>(vtable[1])(base, nullptr, *reinterpret_cast<const char **>(addr::kWagonClassName));
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return nullptr;
	}
}

void AddOutline(Icon &icon)
{
	const int w = icon.w + 2, h = icon.h + 2;
	std::vector<uint32_t> px(static_cast<size_t>(w) * h, 0);
	for (int y = 0; y < icon.h; y++)
		for (int x = 0; x < icon.w; x++) {
			const uint32_t p = icon.px[static_cast<size_t>(y) * icon.w + x];
			if (!p) continue;
			px[static_cast<size_t>(y + 1) * w + x + 1] = p;
			for (int dy = 0; dy <= 2; dy++)
				for (int dx = 0; dx <= 2; dx++) {
					uint32_t &q = px[static_cast<size_t>(y + dy) * w + x + dx];
					if (!q) q = kOutline;
				}
		}
	icon.w = w;
	icon.h = h;
	icon.px.swap(px);
}

bool DecodeBmp(const std::vector<char> &file, Icon &out)
{
	const unsigned char *d = reinterpret_cast<const unsigned char *>(file.data());
	if (file.size() < 54 || d[0] != 'B' || d[1] != 'M') return false;
	const unsigned dataOff = At<unsigned>(d, 10), headerSize = At<unsigned>(d, 14);
	const int w = At<int>(d, 18), rawH = At<int>(d, 22);
	const unsigned short bpp = At<unsigned short>(d, 28);
	const unsigned compression = At<unsigned>(d, 30);
	const int h = rawH < 0 ? -rawH : rawH;
	if (w <= 0 || h <= 0 || w > 64 || h > 64 || (bpp != 16 && bpp != 24 && bpp != 32)) return false;
	bool is565 = false;
	if (compression == BI_BITFIELDS && bpp == 16 && file.size() >= 14 + headerSize + 12) is565 = At<unsigned>(d, 14 + headerSize) == 0xf800;
	else if (compression != BI_RGB) return false;
	const size_t stride = (static_cast<size_t>(w) * bpp / 8 + 3) & ~static_cast<size_t>(3);
	if (dataOff + stride * h > file.size()) return false;
	out.w = w;
	out.h = h;
	out.px.assign(static_cast<size_t>(w) * h, 0);
	for (int y = 0; y < h; y++) {
		const unsigned char *row = d + dataOff + stride * (rawH < 0 ? y : h - 1 - y);
		for (int x = 0; x < w; x++) {
			unsigned r, g, b;
			if (bpp == 16) {
				const unsigned v = row[x * 2] | (row[x * 2 + 1] << 8);
				if (is565) {
					r = (v >> 11) & 31;
					g = ((v >> 5) & 63) >> 1;
				}
				else {
					r = (v >> 10) & 31;
					g = (v >> 5) & 31;
				}
				b = v & 31;
				r = r << 3 | r >> 2;
				g = g << 3 | g >> 2;
				b = b << 3 | b >> 2;
			}
			else {
				const unsigned char *p = row + x * (bpp / 8);
				b = p[0];
				g = p[1];
				r = p[2];
			}
			// The game draws these icons with black as the transparent colour
			if (r < 12 && g < 12 && b < 12) continue;
			out.px[static_cast<size_t>(y) * w + x] = 0xff000000u | r << 16 | g << 8 | b;
		}
	}
	AddOutline(out);
	return true;
}

void LoadIcons()
{
	g_iconsTried = true;
	for (int k = 0; k < 2; k++) {
		std::vector<char> file;
		Icon icon;
		if (ReadGameFile(kIconPaths[k], kReadMode, file) && DecodeBmp(file, icon)) g_icons[k] = icon;
		else LogW("game", "wagon icons: cannot read %s", kIconPaths[k]);
	}
	LogI("game", "wagon icons: food %dx%d, gold %dx%d", g_icons[0].w, g_icons[0].h, g_icons[1].w, g_icons[1].h);
}

void ScanWagons()
{
	g_wagons.clear();
	char **handles = Handles();
	for (unsigned h = 0; h < kHandleCount; h++) {
		char *obj = handles[h];
		if (obj && WagonOf(obj)) g_wagons.push_back({obj, static_cast<unsigned short>(h)});
	}
}

struct View {
	int left = 0, top = 0;
	RECT map = {};
	RECT hud[32] = {};
	int hudCount = 0;
	bool fog = false;
	alignas(16) unsigned char viewer[0x40] = {};
};

bool ReadView(View &v)
{
	__try {
		char *game = *reinterpret_cast<char **>(addr::kGame);
		char *mapView = *reinterpret_cast<char **>(addr::kMapView);
		char *visible = *reinterpret_cast<char **>(addr::kVisibleMgr);
		if (!game || !mapView || !visible || *reinterpret_cast<int *>(addr::kEditorMode) != 0) return false;
		if (!(At<unsigned>(mapView, 0x28) & 2)) return false;
		const short *m = reinterpret_cast<const short *>(mapView + 0x10);
		v.map = {m[0], m[1], m[2] + 1, m[3] + 1};
		v.left = At<int>(visible, kVisibleViewRect) - m[0];
		v.top = At<int>(visible, kVisibleViewRect + 4) - m[1];
		char *player = At<char *>(game, kOffPlayer);
		char *vision = At<char *>(game, kOffVision);
		if (!player) return false;
		v.fog = vision && At<int>(vision, kVisionFogOn) != 0;
		if (v.fog) {
			memcpy(v.viewer, player, sizeof(v.viewer));
			*reinterpret_cast<unsigned *>(v.viewer + 0x14) = At<unsigned>(player, 0x18) | At<unsigned>(player, 0xc);
		}
		// The interface panels are windows of the UI root drawn over the map; an icon under one of them is left out
		char *root = *reinterpret_cast<char **>(addr::kUIRoot);
		const long mapArea = static_cast<long>(v.map.right - v.map.left) * (v.map.bottom - v.map.top);
		for (char *w = root ? At<char *>(root, 0x20) : nullptr; w && v.hudCount < 32; w = At<char *>(w, 0x18)) {
			if (w == mapView || !(At<unsigned>(w, 0x28) & 2)) continue;
			const short *r = reinterpret_cast<const short *>(w + 0x10);
			const RECT rc = {r[0], r[1], r[2] + 1, r[3] + 1};
			const long area = static_cast<long>(rc.right - rc.left) * (rc.bottom - rc.top);
			if (area <= 0 || area * 2 > mapArea) continue;
			v.hud[v.hudCount++] = rc;
		}
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool BadgeOf(const Shown &s, const View &v, Badge &out)
{
	__try {
		char *obj = Handles()[s.handle];
		if (obj != s.obj) return false;
		char *wagon = WagonOf(obj);
		if (!wagon || (At<unsigned>(obj, kObjFlags) & kHiddenFlag)) return false;
		const int amount = At<int>(wagon, kWagonAmount), type = At<int>(wagon, kWagonResType);
		if (amount <= 0 || (type != kFoodType && type != kGoldType)) return false;
		if (v.fog) {
			alignas(16) unsigned char query[32] = {};
			alignas(16) unsigned char viewer[0x40];
			memcpy(viewer, v.viewer, sizeof(viewer));
			int point[2] = {At<int>(obj, kObjPosX), At<int>(obj, kObjPosY)};
			if (!reinterpret_cast<VisionRun_t>(addr::kVisionRun)(query, nullptr, point, viewer)) return false;
		}
		int rect[4] = {};
		reinterpret_cast<ObjScreenRect_t>(addr::kObjScreenRect)(obj, rect);
		if (rect[2] <= rect[0] || rect[3] <= rect[1]) return false;
		out.x = (rect[0] + rect[2]) / 2 - v.left;
		out.y = rect[1] - v.top;
		out.kind = type == kGoldType ? 1 : 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool Overlaps(const RECT &a, const RECT &b) { return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom; }

}

void WagonIconsFrameTick()
{
	std::vector<Badge> badges;
	const DWORD now = GetTickCount();
	View v;
	if (GetConfig().wagonIcons && ReadView(v)) {
		if (!g_iconsTried) LoadIcons();
		if (now - g_scanTick >= kScanMs) {
			g_scanTick = now;
			ScanWagons();
		}
		for (const Shown &s : g_wagons) {
			Badge b;
			if (!BadgeOf(s, v, b)) continue;
			const Icon &icon = g_icons[b.kind];
			if (icon.w == 0) continue;
			const RECT at = {b.x - icon.w / 2, b.y - icon.h - kGap, b.x - icon.w / 2 + icon.w, b.y - kGap};
			if (at.left < v.map.left || at.top < v.map.top || at.right > v.map.right || at.bottom > v.map.bottom) continue;
			bool covered = false;
			for (int i = 0; i < v.hudCount && !covered; i++) covered = Overlaps(at, v.hud[i]);
			if (covered) continue;
			badges.push_back({static_cast<int>(at.left), static_cast<int>(at.top), b.kind});
		}
	}
	std::lock_guard<std::mutex> lock(g_mx);
	g_badges.swap(badges);
	g_badgesTick = now;
}

void WagonIconsDraw(uint32_t *buf, int w, int h)
{
	std::vector<Badge> badges;
	{
		std::lock_guard<std::mutex> lock(g_mx);
		if (GetTickCount() - g_badgesTick > kStaleMs) return;
		badges = g_badges;
	}
	for (const Badge &b : badges) {
		const Icon &icon = g_icons[b.kind];
		for (int y = 0; y < icon.h; y++) {
			const int fy = b.y + y;
			if (fy < 0 || fy >= h) continue;
			for (int x = 0; x < icon.w; x++) {
				const int fx = b.x + x;
				if (fx < 0 || fx >= w) continue;
				const uint32_t p = icon.px[static_cast<size_t>(y) * icon.w + x];
				if (p) buf[static_cast<size_t>(fy) * w + fx] = p;
			}
		}
	}
}
