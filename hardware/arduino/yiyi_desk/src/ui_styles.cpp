#include "ui_styles.h"
#include "ui_widgets.h"   // Ui::Ink / Ui::Green, extern "C" desk_font

namespace UiStyles {

lv_style_t panel, card, button, buttonCaption, titleInk, labelLine2, textCenter,
    bgTransparent, iconTint, slash, sliderTrack, sliderKnob, circle, flat;

namespace {
bool initialised = false;
}

void init() {
    if (initialised) return;
    initialised = true;

    lv_style_init(&panel);
    lv_style_set_border_width(&panel, 0);
    lv_style_set_radius(&panel, 12);
    lv_style_set_pad_all(&panel, 0);

    lv_style_init(&card);
    lv_style_set_border_width(&card, 0);
    lv_style_set_radius(&card, 10);
    lv_style_set_pad_all(&card, 0);

    lv_style_init(&button);
    lv_style_set_shadow_width(&button, 0);
    lv_style_set_border_width(&button, 0);
    lv_style_set_radius(&button, 10);
    lv_style_set_pad_all(&button, 2);

    lv_style_init(&buttonCaption);
    lv_style_set_text_font(&buttonCaption, &desk_font);
    lv_style_set_text_color(&buttonCaption, lv_color_hex(Ui::Ink));
    lv_style_set_text_align(&buttonCaption, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&titleInk);
    lv_style_set_text_font(&titleInk, &desk_font);
    lv_style_set_text_color(&titleInk, lv_color_hex(Ui::Ink));

    lv_style_init(&labelLine2);
    lv_style_set_text_line_space(&labelLine2, 2);

    lv_style_init(&textCenter);
    lv_style_set_text_align(&textCenter, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&bgTransparent);
    lv_style_set_bg_opa(&bgTransparent, LV_OPA_TRANSP);

    lv_style_init(&iconTint);
    lv_style_set_img_recolor_opa(&iconTint, LV_OPA_COVER);

    lv_style_init(&slash);
    lv_style_set_line_width(&slash, 2);
    lv_style_set_line_color(&slash, lv_color_hex(0xAD514C));
    lv_style_set_line_rounded(&slash, true);

    lv_style_init(&sliderTrack);
    lv_style_set_bg_color(&sliderTrack, lv_color_hex(0x414747));

    lv_style_init(&sliderKnob);
    lv_style_set_pad_all(&sliderKnob, 4);

    lv_style_init(&circle);
    lv_style_set_radius(&circle, LV_RADIUS_CIRCLE);

    lv_style_init(&flat);
    lv_style_set_border_width(&flat, 0);
    lv_style_set_shadow_width(&flat, 0);
}

bool ready() { return initialised; }

}  // namespace UiStyles
