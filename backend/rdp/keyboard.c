#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include "backend/rdp.h"
#include "util/signal.h"

struct rdp_to_xkb_keyboard_layout {
	UINT32 rdp_layout_code;
	const char *xkb_layout;
	const char *xkb_variant;
};

/* table reversed from
	https://github.com/awakecoding/FreeRDP/blob/master/libfreerdp/locale/xkb_layout_ids.c#L811 */
static struct rdp_to_xkb_keyboard_layout rdp_keyboards[] = {
	{KBD_ARABIC_101, "ara", 0},
	{KBD_BULGARIAN, 0, 0},
	{KBD_CHINESE_TRADITIONAL_US, 0, 0},
	{KBD_CZECH, "cz", 0},
	{KBD_CZECH_PROGRAMMERS, "cz", "bksl"},
	{KBD_CZECH_QWERTY, "cz", "qwerty"},
	{KBD_DANISH, "dk", 0},
	{KBD_GERMAN, "de", 0},
	{KBD_GERMAN_NEO, "de", "neo"},
	{KBD_GERMAN_IBM, "de", "qwerty"},
	{KBD_GREEK, "gr", 0},
	{KBD_GREEK_220, "gr", "simple"},
	{KBD_GREEK_319, "gr", "extended"},
	{KBD_GREEK_POLYTONIC, "gr", "polytonic"},
	{KBD_US, "us", 0},
	{KBD_US_ENGLISH_TABLE_FOR_IBM_ARABIC_238_L, "ara", "buckwalter"},
	{KBD_SPANISH, "es", 0},
	{KBD_SPANISH_VARIATION, "es", "nodeadkeys"},
	{KBD_FINNISH, "fi", 0},
	{KBD_FRENCH, "fr", 0},
	{KBD_HEBREW, "il", 0},
	{KBD_HUNGARIAN, "hu", 0},
	{KBD_HUNGARIAN_101_KEY, "hu", "standard"},
	{KBD_ICELANDIC, "is", 0},
	{KBD_ITALIAN, "it", 0},
	{KBD_ITALIAN_142, "it", "nodeadkeys"},
	{KBD_JAPANESE, "jp", 0},
	{KBD_JAPANESE_INPUT_SYSTEM_MS_IME2002, "jp", "kana"},
	{KBD_KOREAN, "kr", 0},
	{KBD_KOREAN_INPUT_SYSTEM_IME_2000, "kr", "kr104"},
	{KBD_DUTCH, "nl", 0},
	{KBD_NORWEGIAN, "no", 0},
	{KBD_POLISH_PROGRAMMERS, "pl", 0},
	{KBD_POLISH_214, "pl", "qwertz"},
	{KBD_ROMANIAN, "ro", 0},
	{KBD_RUSSIAN, "ru", 0},
	{KBD_RUSSIAN_TYPEWRITER, "ru", "typewriter"},
	{KBD_CROATIAN, "hr", 0},
	{KBD_SLOVAK, "sk", 0},
	{KBD_SLOVAK_QWERTY, "sk", "qwerty"},
	{KBD_ALBANIAN, 0, 0},
	{KBD_SWEDISH, "se", 0},
	{KBD_THAI_KEDMANEE, "th", 0},
	{KBD_THAI_KEDMANEE_NON_SHIFTLOCK, "th", "tis"},
	{KBD_TURKISH_Q, "tr", 0},
	{KBD_TURKISH_F, "tr", "f"},
	{KBD_URDU, "in", "urd-phonetic3"},
	{KBD_UKRAINIAN, "ua", 0},
	{KBD_BELARUSIAN, "by", 0},
	{KBD_SLOVENIAN, "si", 0},
	{KBD_ESTONIAN, "ee", 0},
	{KBD_LATVIAN, "lv", 0},
	{KBD_LITHUANIAN_IBM, "lt", "ibm"},
	{KBD_FARSI, "af", 0},
	{KBD_VIETNAMESE, "vn", 0},
	{KBD_ARMENIAN_EASTERN, "am", 0},
	{KBD_AZERI_LATIN, 0, 0},
	{KBD_FYRO_MACEDONIAN, "mk", 0},
	{KBD_GEORGIAN, "ge", 0},
	{KBD_FAEROESE, 0, 0},
	{KBD_DEVANAGARI_INSCRIPT, 0, 0},
	{KBD_MALTESE_47_KEY, 0, 0},
	{KBD_NORWEGIAN_WITH_SAMI, "no", "smi"},
	{KBD_KAZAKH, "kz", 0},
	{KBD_KYRGYZ_CYRILLIC, "kg", "phonetic"},
	{KBD_TATAR, "ru", "tt"},
	{KBD_BENGALI, "bd", 0},
	{KBD_BENGALI_INSCRIPT, "bd", "probhat"},
	{KBD_PUNJABI, 0, 0},
	{KBD_GUJARATI, "in", "guj"},
	{KBD_TAMIL, "in", "tam"},
	{KBD_TELUGU, "in", "tel"},
	{KBD_KANNADA, "in", "kan"},
	{KBD_MALAYALAM, "in", "mal"},
	{KBD_HINDI_TRADITIONAL, "in", 0},
	{KBD_MARATHI, 0, 0},
	{KBD_MONGOLIAN_CYRILLIC, "mn", 0},
	{KBD_UNITED_KINGDOM_EXTENDED, "gb", "intl"},
	{KBD_SYRIAC, "syc", 0},
	{KBD_SYRIAC_PHONETIC, "syc", "syc_phonetic"},
	{KBD_NEPALI, "np", 0},
	{KBD_PASHTO, "af", "ps"},
	{KBD_DIVEHI_PHONETIC, 0, 0},
	{KBD_LUXEMBOURGISH, 0, 0},
	{KBD_MAORI, "mao", 0},
	{KBD_CHINESE_SIMPLIFIED_US, 0, 0},
	{KBD_SWISS_GERMAN, "ch", "de_nodeadkeys"},
	{KBD_UNITED_KINGDOM, "gb", 0},
	{KBD_LATIN_AMERICAN, "latam", 0},
	{KBD_BELGIAN_FRENCH, "be", 0},
	{KBD_BELGIAN_PERIOD, "be", "oss_sundeadkeys"},
	{KBD_PORTUGUESE, "pt", 0},
	{KBD_SERBIAN_LATIN, "rs", 0},
	{KBD_AZERI_CYRILLIC, "az", "cyrillic"},
	{KBD_SWEDISH_WITH_SAMI, "se", "smi"},
	{KBD_UZBEK_CYRILLIC, "af", "uz"},
	{KBD_INUKTITUT_LATIN, "ca", "ike"},
	{KBD_CANADIAN_FRENCH_LEGACY, "ca", "fr-legacy"},
	{KBD_SERBIAN_CYRILLIC, "rs", 0},
	{KBD_CANADIAN_FRENCH, "ca", "fr-legacy"},
	{KBD_SWISS_FRENCH, "ch", "fr"},
	{KBD_BOSNIAN, "ba", 0},
	{KBD_IRISH, 0, 0},
	{KBD_BOSNIAN_CYRILLIC, "ba", "us"},
	{KBD_UNITED_STATES_DVORAK, "us", "dvorak"},
	{KBD_PORTUGUESE_BRAZILIAN_ABNT2, "br", "nativo"},
	{KBD_CANADIAN_MULTILINGUAL_STANDARD, "ca", "multix"},
	{KBD_GAELIC, "ie", "CloGaelach"},

	{0x00000000, 0, 0},
};

/* taken from 2.2.7.1.6 Input Capability Set (TS_INPUT_CAPABILITYSET) */
static char *rdp_keyboard_types[] = {
	"",	/* 0: unused */
	"", /* 1: IBM PC/XT or compatible (83-key) keyboard */
	"", /* 2: Olivetti "ICO" (102-key) keyboard */
	"", /* 3: IBM PC/AT (84-key) or similar keyboard */
	"pc102",/* 4: IBM enhanced (101- or 102-key) keyboard */
	"", /* 5: Nokia 1050 and similar keyboards */
	"",	/* 6: Nokia 9140 and similar keyboards */
	""	/* 7: Japanese keyboard */
};

static void keyboard_destroy(struct wlr_input_device *wlr_device) {
	struct wlr_rdp_keyboard *keyboard =
		(struct wlr_rdp_keyboard *)wlr_device->keyboard;
	xkb_keymap_unref(keyboard->keymap);
	free(keyboard);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = keyboard_destroy,
};

struct wlr_rdp_input_device *wlr_rdp_keyboard_create(
		struct wlr_rdp_backend *backend, rdpSettings *settings) {
	struct wlr_rdp_input_device *device =
		calloc(1, sizeof(struct wlr_rdp_input_device));
	if (!device) {
		wlr_log(WLR_ERROR, "Failed to allcoate RDP input device");
		return NULL;
	}

	int vendor = 0;
	int product = 0;
	const char *name = "rdp";
	struct wlr_input_device *wlr_device = &device->wlr_input_device;
	wlr_input_device_init(wlr_device, WLR_INPUT_DEVICE_KEYBOARD,
			&input_device_impl, name, vendor, product);

	struct wlr_rdp_keyboard *keyboard =
		calloc(1, sizeof(struct wlr_rdp_keyboard));
	if (!keyboard) {
		wlr_log(WLR_ERROR, "Failed to allocate RDP pointer device");
		goto error;
	}
	wlr_device->keyboard = (struct wlr_keyboard *)keyboard;
	wlr_keyboard_init(wlr_device->keyboard, NULL);

	wlr_log(WLR_DEBUG, "RDP keyboard layout: 0x%x type: 0x%x subtype: 0x%x "
			"function_keys 0x%x", settings->KeyboardLayout,
			settings->KeyboardType, settings->KeyboardSubType,
			settings->KeyboardFunctionKey);

	// We need to set up an XKB context and jump through some hoops to convert
	// RDP input events into scancodes
	struct xkb_rule_names xkb_rule_names = { 0 };
	if (settings->KeyboardType <= 7) {
		xkb_rule_names.model = rdp_keyboard_types[settings->KeyboardType];
	}
	for (int i = 0; rdp_keyboards[i].rdp_layout_code; ++i) {
		if (rdp_keyboards[i].rdp_layout_code == settings->KeyboardLayout) {
			xkb_rule_names.layout = rdp_keyboards[i].xkb_layout;
			xkb_rule_names.variant = rdp_keyboards[i].xkb_variant;
			wlr_log(WLR_DEBUG, "Mapped RDP keyboard to xkb layout %s variant "
					"%s", xkb_rule_names.layout, xkb_rule_names.variant);
			break;
		}
	}
	if (xkb_rule_names.layout) {
		struct xkb_context *xkb_context = xkb_context_new(0);
		if (!xkb_context) {
			wlr_log(WLR_DEBUG, "Failed to allocate xkb context");
			goto error;
		}
		keyboard->keymap =
			xkb_keymap_new_from_names(xkb_context, &xkb_rule_names, 0);
		xkb_context_unref(xkb_context);
	}

	wlr_keyboard_set_keymap(wlr_device->keyboard, keyboard->keymap);

	wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_device);
	return device;
error:
	wlr_input_device_destroy(wlr_device);
	return NULL;
}
