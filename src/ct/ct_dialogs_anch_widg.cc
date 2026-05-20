/*
 * ct_dialogs_anch_widg.cc
 *
 * Copyright 2009-2025
 * Giuseppe Penone <giuspen@gmail.com>
 * Evgenii Gurianov <https://github.com/txe>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "ct_dialogs.h"
#include "ct_main_win.h"
#include "ct_text_view.h"
#include "ct_table.h"

#if GTKMM_MAJOR_VERSION < 4 && !defined(GTKMM_DISABLE_DEPRECATED)
Glib::ustring CtDialogs::latex_handle_dialog(CtMainWin* pCtMainWin,
                                             const Glib::ustring& latex_text)
{
    CtTextView textView{pCtMainWin};
    Glib::RefPtr<Gtk::TextBuffer> rBuffer = textView.get_buffer();
    rBuffer->set_text(latex_text);
    textView.setup_for_syntax("latex");
    pCtMainWin->apply_syntax_highlighting(rBuffer, "latex", false/*forceReApply*/);
    auto scrolledwindow = Gtk::manage(new Gtk::ScrolledWindow{});
    scrolledwindow->set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scrolledwindow->add(textView.mm());
    Gtk::Dialog dialog{_("Latex Text"),
                       *pCtMainWin,
                       Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_DESTROY_WITH_PARENT};

    (void)CtMiscUtil::dialog_add_button(&dialog, _("Cancel"), Gtk::RESPONSE_REJECT, "ct_cancel");
    (void)CtMiscUtil::dialog_add_button(&dialog, _("OK"), Gtk::RESPONSE_ACCEPT, "ct_done", true/*isDefault*/);

    dialog.set_position(Gtk::WindowPosition::WIN_POS_CENTER_ON_PARENT);
    dialog.set_default_size(400, 250);
    Gtk::Box* pContentArea = dialog.get_content_area();
    auto hbox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_HORIZONTAL, 2/*spacing*/});
    auto vbox = Gtk::manage(new Gtk::Box{Gtk::ORIENTATION_VERTICAL, 2/*spacing*/});
    hbox->pack_start(*scrolledwindow);
    hbox->pack_start(*vbox, false, false);
    auto pCtConfig = pCtMainWin->get_ct_config();
    auto label_latex_size = Gtk::manage(new Gtk::Label{_("Image Size dpi")});
    Glib::RefPtr<Gtk::Adjustment> adj_latex_size = Gtk::Adjustment::create(pCtConfig->latexSizeDpi, 1, 10000, 10);
    auto spinbutton_latex_size = Gtk::manage(new Gtk::SpinButton{adj_latex_size});
    vbox->pack_end(*spinbutton_latex_size, false, false);
    vbox->pack_end(*label_latex_size, false, false);
    auto button_latex_tutorial = Gtk::manage(new Gtk::Button{});
    auto button_latex_reference = Gtk::manage(new Gtk::Button{});
    button_latex_tutorial->set_label(_("Tutorial"));
    button_latex_reference->set_label(_("Reference"));
    button_latex_tutorial->set_image_from_icon_name("ct_link_website", Gtk::ICON_SIZE_MENU);
    button_latex_reference->set_image_from_icon_name("ct_link_website", Gtk::ICON_SIZE_MENU);
    button_latex_tutorial->set_tooltip_text(_("LaTeX Math and Equations Tutorial"));
    button_latex_reference->set_tooltip_text(_("LaTeX Math Symbols Reference"));
    button_latex_tutorial->set_always_show_image(true);
    button_latex_reference->set_always_show_image(true);
    vbox->pack_start(*button_latex_tutorial, false, false);
    vbox->pack_start(*button_latex_reference, false, false);
    Glib::ustring error_msg = CtImageLatex::getRenderingErrorMessage();
    if (not error_msg.empty()) {
        auto p_label_error_msg = Gtk::manage(new Gtk::Label{error_msg});
        p_label_error_msg->set_use_markup(true);
        pContentArea->pack_start(*p_label_error_msg);
    }
    pContentArea->pack_start(*hbox);
    spinbutton_latex_size->signal_value_changed().connect([pCtMainWin, spinbutton_latex_size](){
        pCtMainWin->get_ct_config()->latexSizeDpi = spinbutton_latex_size->get_value_as_int();
    });
    button_latex_tutorial->signal_clicked().connect([](){
        fs::open_weblink("https://latex-tutorial.com/tutorials/amsmath/");
    });
    button_latex_reference->signal_clicked().connect([](){
        fs::open_weblink("https://latex-tutorial.com/symbols/math-symbols/");
    });
    auto on_key_press_dialog = [&](GdkEventKey* pEventKey)->bool{
        if (GDK_KEY_Escape == pEventKey->keyval) {
            Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_REJECT));
            pButton->grab_focus();
            pButton->clicked();
            return true;
        }
        return false;
    };
    dialog.signal_key_press_event().connect(on_key_press_dialog, false/*call me before other*/);
    pContentArea->show_all();
    return Gtk::RESPONSE_ACCEPT == dialog.run() ? rBuffer->get_text() : "";
}

class CropImage : public Gtk::Image {

public:
    CropImage(Glib::RefPtr<Gdk::Pixbuf> pixbuf) :
            Gtk::Image(pixbuf) {

        x = 0;
        y = 0;
        w = pixbuf->get_width();
        h = pixbuf->get_height();
        moved = false;

        set_has_window(true);

        add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK |
            Gdk::POINTER_MOTION_MASK);

    }

    void set(Glib::RefPtr<Gdk::Pixbuf> pixbuf) {

        int old_width = get_pixbuf()->get_width();
        int old_height = get_pixbuf()->get_height();
        int new_width = pixbuf->get_width();
        int new_height = pixbuf->get_height();

        Gtk::Image::set(pixbuf);

        x *= (double) new_width / old_width;
        w *= (double) new_width / old_width;
        y *= (double) new_height / old_height;
        h *= (double) new_height / old_height;

    }

    /* Get result x/y/w/h given that actual image dimensions are
     * width × height */
    void get_crop(int width, int height,
                  double* rx, double* ry, double* rw, double* rh) {

        if (w < 0) {
            x += w;
            w *= -1;
        }
        if (h < 0) {
            y += h;
            h *= -1;
        }

        double wscale = (double) width / get_pixbuf()->get_width();
        double hscale = (double) height / get_pixbuf()->get_height();

        if (rx) *rx = x * wscale;
        if (rw) *rw = w * wscale;
        if (ry) *ry = y * hscale;
        if (rh) *rh = h * hscale;

    }

protected:

    bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override {

        cr->save();
        Gdk::Cairo::set_source_pixbuf( cr, get_pixbuf(), 0, 0 );
        cr->paint();
        cr->restore();

        int img_w = get_pixbuf()->get_width();
        int img_h = get_pixbuf()->get_height();

        cr->save();
        cr->set_source_rgba(0, 0, 0, 0.25);
        cr->set_fill_rule(Cairo::FILL_RULE_EVEN_ODD);
        cr->rectangle(0, 0, img_w, img_h);
        cr->rectangle(x, y, w, h);
        cr->fill();
        cr->restore();

        return false;

    }

    bool on_motion_notify_event(GdkEventMotion* event) override {

        if (event->state & GDK_BUTTON1_MASK) {

            moved = true;
            w = event->x - x;
            h = event->y - y;
            queue_draw();

        }

        return true;

    }

    bool on_button_press_event(GdkEventButton* event) override {

        if (GDK_BUTTON_PRIMARY == event->button) {
            x = event->x;
            y = event->y;
            w = 0;
            h = 0;
            moved = false;
        }
        else if (GDK_BUTTON_SECONDARY == event->button) {
            x = 0;
            y = 0;
            w = get_pixbuf()->get_width();
            h = get_pixbuf()->get_height();
        }

        queue_draw();

        return true;

    }

    bool on_button_release_event(GdkEventButton* event) override {

        if (GDK_BUTTON_PRIMARY == event->button && !moved) {
            x = 0;
            y = 0;
            w = get_pixbuf()->get_width();
            h = get_pixbuf()->get_height();
            queue_draw();
        }

        return true;

    }

private:
    int x, y, w, h;
    bool moved;

};

Glib::RefPtr<Gdk::Pixbuf> CtDialogs::image_handle_dialog(Gtk::Window& parent_win,
                                                         Glib::RefPtr<Gdk::Pixbuf> rOriginalPixbuf,
                                                         Glib::RefPtr<Gdk::Pixbuf> rHighResPixbuf)
{
    // In headless/test mode, simulate "accept without size change using the high-res source"
    // — same logic as the real no-crop path below. This lets automated tests exercise the
    // rHighResPixbuf code path without a real display or user interaction.
    if (rHighResPixbuf) {
        if (auto* pCtMainWin = dynamic_cast<CtMainWin*>(&parent_win)) {
            if (pCtMainWin->no_gui()) {
                return rHighResPixbuf->scale_simple(
                    rOriginalPixbuf->get_width(), rOriginalPixbuf->get_height(), Gdk::INTERP_BILINEAR);
            }
        }
    }

    int width = rOriginalPixbuf->get_width();
    int height = rOriginalPixbuf->get_height();
    double image_w_h_ration = static_cast<double>(width)/height;
    int orig_width = rHighResPixbuf ? rHighResPixbuf->get_width() : width;
    int orig_height = rHighResPixbuf ? rHighResPixbuf->get_height() : height;
    bool lock_aspect = true;
    bool is_percentage = false;
    CtConfig* pConfig = nullptr;
    if (auto* pCtMainWin = dynamic_cast<CtMainWin*>(&parent_win)) {
        pConfig = pCtMainWin->get_ct_config();
    }

    Gtk::Dialog dialog{_("Image Properties"),
                       parent_win,
                       Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_DESTROY_WITH_PARENT};

    (void)CtMiscUtil::dialog_add_button(&dialog, _("Cancel"), Gtk::RESPONSE_REJECT, "ct_cancel");
    (void)CtMiscUtil::dialog_add_button(&dialog, _("OK"), Gtk::RESPONSE_ACCEPT, "ct_done", true/*isDefault*/);

    dialog.set_position(Gtk::WindowPosition::WIN_POS_CENTER_ON_PARENT);
    dialog.set_default_size(600, 500);
    Gtk::Button button_rotate_90_ccw;
    button_rotate_90_ccw.set_image_from_icon_name("ct_rotate-left", Gtk::ICON_SIZE_DND);
    button_rotate_90_ccw.set_tooltip_text(_("Rotate Left"));
    Gtk::Button button_rotate_90_cw;
    button_rotate_90_cw.set_image_from_icon_name("ct_rotate-right", Gtk::ICON_SIZE_DND);
    button_rotate_90_cw.set_tooltip_text(_("Rotate Right"));
    Gtk::ScrolledWindow scrolledwindow;
    scrolledwindow.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    Glib::RefPtr<Gtk::Adjustment> rHAdj = Gtk::Adjustment::create(width, 1, height, 1);
    Glib::RefPtr<Gtk::Adjustment> rVAdj = Gtk::Adjustment::create(width, 1, width, 1);
    Gtk::Viewport viewport(rHAdj, rVAdj);
    CropImage image{rOriginalPixbuf};
    scrolledwindow.add(viewport);
    viewport.add(image);
    Gtk::Box hbox_1{Gtk::ORIENTATION_HORIZONTAL, 2/*spacing*/};
    hbox_1.pack_start(button_rotate_90_ccw, false, false);
    hbox_1.pack_start(scrolledwindow);
    hbox_1.pack_start(button_rotate_90_cw, false, false);
    Gtk::Button button_crop;
    button_crop.set_image_from_icon_name("ct_edit_cut", Gtk::ICON_SIZE_DND);
    button_crop.set_tooltip_text(_("In order to crop the image, select the area with the mouse before clicking OK"));
    Gtk::Button button_flip_horizontal;
    button_flip_horizontal.set_image_from_icon_name("ct_flip-horizontal", Gtk::ICON_SIZE_DND);
    button_flip_horizontal.set_tooltip_text(_("Flip Horizontally"));
    Gtk::Button button_flip_vertical;
    button_flip_vertical.set_image_from_icon_name("ct_flip-vertical", Gtk::ICON_SIZE_DND);
    button_flip_vertical.set_tooltip_text(_("Flip Vertically"));
    Gtk::Box hbox_2{Gtk::ORIENTATION_HORIZONTAL, 2/*spacing*/};
    hbox_2.pack_start(button_flip_horizontal, true, true);
    hbox_2.pack_start(button_crop, true, true);
    hbox_2.pack_start(button_flip_vertical, true, true);
    Gtk::Label label_width{_("Width")};
    Glib::RefPtr<Gtk::Adjustment> rAdj_width = Gtk::Adjustment::create(width, 1, 10000, 1);
    Gtk::SpinButton spinbutton_width{rAdj_width};
    Gtk::Label label_height{_("Height")};
    Glib::RefPtr<Gtk::Adjustment> rAdj_height = Gtk::Adjustment::create(height, 1, 10000, 1);
    Gtk::SpinButton spinbutton_height{rAdj_height};
    Gtk::Box hbox_3{Gtk::ORIENTATION_HORIZONTAL};
    hbox_3.pack_start(label_width);
    hbox_3.pack_start(spinbutton_width);
    hbox_3.pack_start(label_height);
    hbox_3.pack_start(spinbutton_height);
    Gtk::ComboBoxText combobox_unit;
    combobox_unit.append(_("Pixels"));
    combobox_unit.append(_("Percentage"));
    if (pConfig && !pConfig->imageSizeUnitPixels) {
        is_percentage = true;
        combobox_unit.set_active(1);
        rAdj_width->set_upper(1000);
        rAdj_height->set_upper(1000);
    } else {
        combobox_unit.set_active(0);
    }
    Gtk::CheckButton checkbutton_lock_ratio{_("Lock aspect ratio")};
    checkbutton_lock_ratio.set_active(true);
    Gtk::Box* pContentArea = dialog.get_content_area();
    pContentArea->pack_start(hbox_1);
    pContentArea->pack_start(hbox_2, false, false);
    pContentArea->pack_start(hbox_3, false, false);
    pContentArea->set_spacing(6);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    auto* pButtonBox = dynamic_cast<Gtk::ButtonBox*>(dialog.get_action_area());
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (pButtonBox) {
        pButtonBox->pack_start(combobox_unit, false, false);
        pButtonBox->set_child_secondary(combobox_unit, true);
        pButtonBox->pack_start(checkbutton_lock_ratio, false, false);
        pButtonBox->set_child_secondary(checkbutton_lock_ratio, true);
    }

    bool stop_update = false;
    auto image_load_into_dialog = [&]() {
        stop_update = true;
        if (is_percentage) {
            spinbutton_width.set_value(100.0 * width / orig_width);
            spinbutton_height.set_value(100.0 * height / orig_height);
        } else {
            spinbutton_width.set_value(width);
            spinbutton_height.set_value(height);
        }
        Glib::RefPtr<Gdk::Pixbuf> rPixbuf;
        if (width <= 900 && height <= 600) {
            // original size into the dialog
            rPixbuf = rOriginalPixbuf->scale_simple(width, height, Gdk::INTERP_BILINEAR);
        }
        else {
            // reduced size visible into the dialog
            if (width > 900) {
                int img_parms_width = 900;
                int img_parms_height = (int)(img_parms_width * height / (double)width);
                rPixbuf = rOriginalPixbuf->scale_simple(img_parms_width, img_parms_height, Gdk::INTERP_BILINEAR);
            }
            else {
                int img_parms_height = 600;
                int img_parms_width = (int)(img_parms_height * width / (double)height);
                rPixbuf = rOriginalPixbuf->scale_simple(img_parms_width, img_parms_height, Gdk::INTERP_BILINEAR);
            }
        }
        image.set(rPixbuf);
        stop_update = false;
    };
    button_rotate_90_cw.signal_clicked().connect([&](){
        rOriginalPixbuf = rOriginalPixbuf->rotate_simple(Gdk::PixbufRotation::PIXBUF_ROTATE_CLOCKWISE);
        image_w_h_ration = 1./image_w_h_ration;
        std::swap(width, height);
        std::swap(orig_width, orig_height);
        image_load_into_dialog();
    });
    button_rotate_90_ccw.signal_clicked().connect([&](){
        rOriginalPixbuf = rOriginalPixbuf->rotate_simple(Gdk::PixbufRotation::PIXBUF_ROTATE_COUNTERCLOCKWISE);
        image_w_h_ration = 1./image_w_h_ration;
        std::swap(width, height);
        std::swap(orig_width, orig_height);
        image_load_into_dialog();
    });
    button_flip_horizontal.signal_clicked().connect([&](){
        rOriginalPixbuf = rOriginalPixbuf->flip(true);
        image_load_into_dialog();
    });
    button_crop.signal_clicked().connect([&](){
        CtDialogs::info_dialog(_("In order to crop the image, select the area with the mouse before clicking OK"), dialog);
    });
    button_flip_vertical.signal_clicked().connect([&](){
        rOriginalPixbuf = rOriginalPixbuf->flip(false);
        image_load_into_dialog();
    });
    spinbutton_width.signal_value_changed().connect([&](){
        if (stop_update) return;
        if (is_percentage) {
            double pct = spinbutton_width.get_value();
            width = std::max(1, static_cast<int>(orig_width * pct / 100.0));
            if (lock_aspect) {
                height = std::max(1, static_cast<int>(orig_height * pct / 100.0));
            }
        } else {
            width = spinbutton_width.get_value_as_int();
            if (lock_aspect) {
                height = static_cast<int>(width / image_w_h_ration);
            }
        }
        image_load_into_dialog();
    });
    spinbutton_height.signal_value_changed().connect([&](){
        if (stop_update) return;
        if (is_percentage) {
            double pct = spinbutton_height.get_value();
            height = std::max(1, static_cast<int>(orig_height * pct / 100.0));
            if (lock_aspect) {
                width = std::max(1, static_cast<int>(orig_width * pct / 100.0));
            }
        } else {
            height = spinbutton_height.get_value_as_int();
            if (lock_aspect) {
                width = static_cast<int>(height * image_w_h_ration);
            }
        }
        image_load_into_dialog();
    });
    combobox_unit.signal_changed().connect([&](){
        stop_update = true;
        is_percentage = (combobox_unit.get_active_row_number() == 1);
        if (pConfig) {
            pConfig->imageSizeUnitPixels = !is_percentage;
        }
        if (is_percentage) {
            rAdj_width->set_upper(1000);
            rAdj_height->set_upper(1000);
            spinbutton_width.set_value(100.0 * width / orig_width);
            spinbutton_height.set_value(100.0 * height / orig_height);
        } else {
            rAdj_width->set_upper(10000);
            rAdj_height->set_upper(10000);
            spinbutton_width.set_value(width);
            spinbutton_height.set_value(height);
        }
        stop_update = false;
    });
    checkbutton_lock_ratio.signal_toggled().connect([&](){
        lock_aspect = checkbutton_lock_ratio.get_active();
        if (lock_aspect) {
            image_w_h_ration = static_cast<double>(width) / height;
        }
    });
    auto on_key_press_dialog = [&](GdkEventKey* pEventKey)->bool{
        if (GDK_KEY_Return == pEventKey->keyval or GDK_KEY_KP_Enter == pEventKey->keyval) {
            Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_ACCEPT));
            pButton->grab_focus();
            pButton->clicked();
            return true;
        }
        if (GDK_KEY_Escape == pEventKey->keyval) {
            Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_REJECT));
            pButton->grab_focus();
            pButton->clicked();
            return true;
        }
        return false;
    };
    dialog.signal_key_press_event().connect(on_key_press_dialog, false/*call me before other*/);
    image_load_into_dialog();
    pContentArea->show_all();
    if ( Gtk::RESPONSE_ACCEPT == dialog.run() ) {
        double x, y, w, h;
        image.get_crop( width, height, &x, &y, &w, &h );
        // If a high-res original is available and no crop was applied, scale from it for better quality
        const bool noCrop = (x < 0.5 && y < 0.5 && std::abs(w - width) < 0.5 && std::abs(h - height) < 0.5);
        if (rHighResPixbuf && noCrop) {
            return rHighResPixbuf->scale_simple(width, height, Gdk::INTERP_BILINEAR);
        }
        Glib::RefPtr<Gdk::Pixbuf> rPixbuf = Gdk::Pixbuf::create(
            rOriginalPixbuf->get_colorspace(),
            rOriginalPixbuf->get_has_alpha(),
            rOriginalPixbuf->get_bits_per_sample(),
            w, h );
        rOriginalPixbuf->scale( rPixbuf,
            0, 0, /* Top left X & Y on dest pixbuf */
            w, h, /* Width & height of destination image */
            - x, - y, /* Top left on src, after scaling */
            (double) width / rOriginalPixbuf->get_width(), /* Scale */
            (double) height / rOriginalPixbuf->get_height(),
            Gdk::INTERP_BILINEAR );
        return rPixbuf;
    } else {
        return Glib::RefPtr<Gdk::Pixbuf>{};
    }
}

bool CtDialogs::codeboxhandle_dialog(CtMainWin* pCtMainWin,
                                     const Glib::ustring& title)
{
    Gtk::Dialog dialog{title,
                       *pCtMainWin,
                       Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_DESTROY_WITH_PARENT};

    (void)CtMiscUtil::dialog_add_button(&dialog, _("Cancel"), Gtk::RESPONSE_REJECT, "ct_cancel");
    (void)CtMiscUtil::dialog_add_button(&dialog, _("OK"), Gtk::RESPONSE_ACCEPT, "ct_done");

    dialog.set_default_size(300, -1);
    dialog.set_position(Gtk::WIN_POS_CENTER_ON_PARENT);

    CtConfig* pConfig = pCtMainWin->get_ct_config();

    Gtk::Button button_prog_lang;
    const Glib::ustring syntax_hl_id = pConfig->codeboxSynHighl != CtConst::PLAIN_TEXT_ID ? pConfig->codeboxSynHighl : pConfig->autoSynHighl;
    const std::string stock_id = pCtMainWin->get_code_icon_name(syntax_hl_id);
    button_prog_lang.set_label(syntax_hl_id);
    button_prog_lang.set_image(*pCtMainWin->new_managed_image_from_stock(stock_id, Gtk::ICON_SIZE_MENU));
    Gtk::RadioButton radiobutton_plain_text{_("Plain Text")};
    Gtk::RadioButton radiobutton_auto_syntax_highl{_("Automatic Syntax Highlighting")};
    radiobutton_auto_syntax_highl.join_group(radiobutton_plain_text);
    if (pConfig->codeboxSynHighl == CtConst::PLAIN_TEXT_ID) {
        radiobutton_plain_text.set_active(true);
        button_prog_lang.set_sensitive(false);
    }
    else {
        radiobutton_auto_syntax_highl.set_active(true);
    }
    Gtk::Box type_vbox{Gtk::ORIENTATION_VERTICAL};
    type_vbox.pack_start(radiobutton_plain_text);
    type_vbox.pack_start(radiobutton_auto_syntax_highl);
    type_vbox.pack_start(button_prog_lang);
    Gtk::Frame type_frame{Glib::ustring("<b>")+_("Type")+"</b>"};
    dynamic_cast<Gtk::Label*>(type_frame.get_label_widget())->set_use_markup(true);
    type_frame.set_shadow_type(Gtk::SHADOW_NONE);
    type_frame.add(type_vbox);

    Gtk::Label label_width{_("Width")};
    Glib::RefPtr<Gtk::Adjustment> rAdj_width = Gtk::Adjustment::create(pConfig->codeboxWidth, 1, 10000);
    Gtk::SpinButton spinbutton_width{rAdj_width};
    spinbutton_width.set_value(pConfig->codeboxWidth);
    Gtk::Label label_height{_("Height")};
    Glib::RefPtr<Gtk::Adjustment> rAdj_height = Gtk::Adjustment::create(pConfig->codeboxHeight, 1, 10000);
    Gtk::SpinButton spinbutton_height{rAdj_height};
    spinbutton_height.set_value(pConfig->codeboxHeight);

    Gtk::RadioButton radiobutton_codebox_pixels{_("pixels")};
    Gtk::RadioButton radiobutton_codebox_percent{"%"};
    radiobutton_codebox_percent.join_group(radiobutton_codebox_pixels);
    radiobutton_codebox_pixels.set_active(pConfig->codeboxWidthPixels);
    radiobutton_codebox_percent.set_active(!pConfig->codeboxWidthPixels);

    Gtk::Box vbox_pix_perc{Gtk::ORIENTATION_VERTICAL};
    vbox_pix_perc.pack_start(radiobutton_codebox_pixels);
    vbox_pix_perc.pack_start(radiobutton_codebox_percent);
    Gtk::Box hbox_width{Gtk::ORIENTATION_HORIZONTAL, 5/*spacing*/};
    hbox_width.pack_start(label_width, false, false);
    hbox_width.pack_start(spinbutton_width, false, false);
    hbox_width.pack_start(vbox_pix_perc);
    Gtk::Box hbox_height{Gtk::ORIENTATION_HORIZONTAL, 5/*spacing*/};
    hbox_height.pack_start(label_height, false, false);
    hbox_height.pack_start(spinbutton_height, false, false);
    Gtk::Box vbox_size{Gtk::ORIENTATION_VERTICAL};
    vbox_size.pack_start(hbox_width);
    vbox_size.pack_start(hbox_height);
    CtMiscUtil::set_widget_margins(vbox_size, 0, 6, 6, 6);

    Gtk::Frame size_frame{Glib::ustring("<b>")+_("Size")+"</b>"};
    dynamic_cast<Gtk::Label*>(size_frame.get_label_widget())->set_use_markup(true);
    size_frame.set_shadow_type(Gtk::SHADOW_NONE);
    size_frame.add(vbox_size);

    Gtk::CheckButton checkbutton_codebox_linenumbers{_("Show Line Numbers")};
    checkbutton_codebox_linenumbers.set_active(pConfig->codeboxLineNum);
    Gtk::CheckButton checkbutton_codebox_matchbrackets{_("Highlight Matching Brackets")};
    checkbutton_codebox_matchbrackets.set_active(pConfig->codeboxMatchBra);
    Gtk::Box vbox_options{Gtk::ORIENTATION_VERTICAL};
    vbox_options.pack_start(checkbutton_codebox_linenumbers);
    vbox_options.pack_start(checkbutton_codebox_matchbrackets);
    CtMiscUtil::set_widget_margins(vbox_options, 6, 6, 6, 6);

    Gtk::Frame options_frame{Glib::ustring("<b>")+_("Options")+"</b>"};
    dynamic_cast<Gtk::Label*>(options_frame.get_label_widget())->set_use_markup(true);
    options_frame.set_shadow_type(Gtk::SHADOW_NONE);
    options_frame.add(vbox_options);

    Gtk::Box* pContentArea = dialog.get_content_area();
    pContentArea->set_spacing(5);
    pContentArea->pack_start(type_frame);
    pContentArea->pack_start(size_frame);
    pContentArea->pack_start(options_frame);
    pContentArea->show_all();

    button_prog_lang.signal_clicked().connect([&button_prog_lang, &dialog, pCtMainWin, pConfig](){
        Glib::RefPtr<CtChooseDialogListStore> rItemStore = CtChooseDialogListStore::create();
        unsigned pathSelectIdx{0};
        unsigned pathCurrIdx{0};
        const auto currSyntaxHighl = button_prog_lang.get_label();
        const gchar * const * pLanguageIDs = gtk_source_language_manager_get_language_ids(pCtMainWin->get_language_manager());
        for (auto pLang = pLanguageIDs; *pLang; ++pLang) {
            rItemStore->add_row(pCtMainWin->get_code_icon_name(*pLang), "", *pLang);
            if (*pLang == currSyntaxHighl) {
                pathSelectIdx = pathCurrIdx;
            }
            ++pathCurrIdx;
        }
        Gtk::TreeModel::iterator res = CtDialogs::choose_item_dialog(dialog,
                                                          _("Automatic Syntax Highlighting"),
                                                          rItemStore,
                                                          nullptr/*single_column_name*/,
                                                          std::to_string(pathSelectIdx),
                                                          std::make_pair(200, pConfig->winRect[3]));
        if (res) {
            const Glib::ustring syntax_hl_id = res->get_value(rItemStore->columns.desc);
            const std::string stock_id = pCtMainWin->get_code_icon_name(syntax_hl_id);
            button_prog_lang.set_label(syntax_hl_id);
            button_prog_lang.set_image(*pCtMainWin->new_managed_image_from_stock(stock_id, Gtk::ICON_SIZE_MENU));
        }
    });
    radiobutton_auto_syntax_highl.signal_toggled().connect([&radiobutton_auto_syntax_highl, &button_prog_lang](){
        button_prog_lang.set_sensitive(radiobutton_auto_syntax_highl.get_active());
    });
    dialog.signal_key_press_event().connect([&](GdkEventKey* pEventKey){
        if (GDK_KEY_Return == pEventKey->keyval or GDK_KEY_KP_Enter == pEventKey->keyval) {
            spinbutton_width.update();
            spinbutton_height.update();
            dialog.response(Gtk::RESPONSE_ACCEPT);
            return true;
        }
        return false;
    });
    radiobutton_codebox_pixels.signal_toggled().connect([&radiobutton_codebox_pixels, &spinbutton_width](){
        if (radiobutton_codebox_pixels.get_active()) {
            spinbutton_width.set_value(700);
        }
        else if (spinbutton_width.get_value() > 100) {
            spinbutton_width.set_value(90);
        }
    });
    auto on_key_press_dialog = [&](GdkEventKey* pEventKey)->bool{
        if (GDK_KEY_Return == pEventKey->keyval or GDK_KEY_KP_Enter == pEventKey->keyval) {
            Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_ACCEPT));
            pButton->grab_focus();
            pButton->clicked();
            return true;
        }
        if (GDK_KEY_Escape == pEventKey->keyval) {
            Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_REJECT));
            pButton->grab_focus();
            pButton->clicked();
            return true;
        }
        return false;
    };
    dialog.signal_key_press_event().connect(on_key_press_dialog, false/*call me before other*/);

    const int response = dialog.run();
    dialog.hide();

    if (response == Gtk::RESPONSE_ACCEPT) {
        pConfig->codeboxWidth = spinbutton_width.get_value_as_int();
        pConfig->codeboxWidthPixels = radiobutton_codebox_pixels.get_active();
        pConfig->codeboxHeight = spinbutton_height.get_value();
        pConfig->codeboxLineNum = checkbutton_codebox_linenumbers.get_active();
        pConfig->codeboxMatchBra = checkbutton_codebox_matchbrackets.get_active();
        if (radiobutton_plain_text.get_active()) {
            pConfig->codeboxSynHighl = CtConst::PLAIN_TEXT_ID;
        }
        else {
            pConfig->codeboxSynHighl = button_prog_lang.get_label();
        }
        return true;
    }
    return false;
}

CtDialogs::TableHandleResp CtDialogs::table_handle_dialog(CtMainWin* pCtMainWin,
                                                          const Glib::ustring& title,
                                                          const bool is_insert,
                                                          bool& is_light,
                                                          bool& is_rich,
                                                          CtTableStyle* pTableStyle,
                                                          size_t currentRow,
                                                          size_t currentCol,
                                                          size_t numRows,
                                                          size_t numCols,
                                                          int currentColWidth)
{
    Gtk::Dialog dialog{title,
                       *pCtMainWin,
                       Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_DESTROY_WITH_PARENT};
    dialog.set_transient_for(*pCtMainWin);

    (void)CtMiscUtil::dialog_add_button(&dialog, _("Cancel"), Gtk::RESPONSE_REJECT, "ct_cancel");
    (void)CtMiscUtil::dialog_add_button(&dialog, _("OK"), Gtk::RESPONSE_ACCEPT, "ct_done", true/*isDefault*/);

    dialog.set_position(Gtk::WindowPosition::WIN_POS_CENTER_ON_PARENT);
    dialog.set_default_size(300, -1);

    auto pCtConfig = pCtMainWin->get_ct_config();
    auto label_rows = Gtk::Label{_("Rows")};
    label_rows.set_halign(Gtk::Align::ALIGN_START);
    label_rows.set_margin_start(10);
    auto adj_rows = Gtk::Adjustment::create(pCtConfig->tableRows, 1, 10000, 1);
    auto spinbutton_rows = Gtk::SpinButton{adj_rows};
    spinbutton_rows.set_value(pCtConfig->tableRows);
    auto label_columns = Gtk::Label{_("Columns")};
    label_columns.set_halign(Gtk::Align::ALIGN_START);
    auto adj_columns = Gtk::Adjustment::create(pCtConfig->tableColumns, 1, 10000, 1);
    auto spinbutton_columns = Gtk::SpinButton{adj_columns};
    spinbutton_columns.set_value(pCtConfig->tableColumns);

    auto label_col_width = Gtk::Label{_("Min Width")};
    label_col_width.set_halign(Gtk::Align::ALIGN_START);
    auto adj_col_width = Gtk::Adjustment::create(pCtConfig->tableColWidthDefault, 1, 10000, 1);
    auto spinbutton_col_width = Gtk::SpinButton{adj_col_width};
    spinbutton_col_width.set_value(pCtConfig->tableColWidthDefault);

    auto label_row_height = Gtk::Label{_("Min Height")};
    label_row_height.set_halign(Gtk::Align::ALIGN_START);
    auto adj_row_height = Gtk::Adjustment::create(pCtConfig->tableRowHeightDefault, 0, 2000, 1);
    auto spinbutton_row_height_ins = Gtk::SpinButton{adj_row_height};
    spinbutton_row_height_ins.set_value(pCtConfig->tableRowHeightDefault);

    auto label_size = Gtk::Label{std::string("<b>")+_("Table Size")+"</b>"};
    label_size.set_use_markup();
    label_size.set_halign(Gtk::Align::ALIGN_START);
    auto label_col = Gtk::Label{std::string("<b>")+_("Column Properties")+"</b>"};
    label_col.set_use_markup();
    label_col.set_halign(Gtk::Align::ALIGN_START);

    Gtk::Grid grid;
    grid.property_margin() = 6;
    grid.set_row_spacing(4);
    grid.set_column_spacing(8);
    grid.set_row_homogeneous(true);

    if (is_insert) {
        grid.attach(label_size,              0, 0, 2, 1);
        grid.attach(label_rows,              0, 1, 1, 1);
        grid.attach(spinbutton_rows,         1, 1, 1, 1);
        grid.attach(label_columns,           2, 1, 1, 1);
        grid.attach(spinbutton_columns,      3, 1, 1, 1);
    }
    if (!pTableStyle) {
        // Insert mode or non-rich edit: show the standalone Default Width spinbutton.
        // Rich edit mode uses the Column Width frame instead.
        grid.attach(label_col,             0, 2, 2, 1);
        grid.attach(label_col_width,       0, 3, 1, 1);
        grid.attach(spinbutton_col_width,  1, 3, 1, 1);
    }
    if (is_insert) {
        grid.attach(label_row_height,           2, 3, 1, 1);
        grid.attach(spinbutton_row_height_ins,  3, 3, 1, 1);
    }

    auto checkbutton_is_light = Gtk::CheckButton(_("Lightweight Interface (much faster for large tables)"));
    checkbutton_is_light.set_active(is_light);
    auto checkbutton_is_rich = Gtk::CheckButton(_("Rich Table (supports bold, italic, images, etc.)"));
    checkbutton_is_rich.set_active(is_rich);
    if (is_rich) checkbutton_is_light.set_sensitive(false);
    auto checkbutton_table_ins_from_file = Gtk::CheckButton(_("Import from CSV File"));

    if (is_insert) {
        // Default row height only applies to rich tables — light/heavy tables
        // ignore tableRowHeightDefault entirely. Gate the spinbutton state on
        // the Rich-Table checkbox so the user can see at a glance that it's
        // a rich-table-only setting.
        auto sync_row_height_sensitive = [&checkbutton_is_rich, &spinbutton_row_height_ins, &label_row_height]() {
            const bool active = checkbutton_is_rich.get_active();
            spinbutton_row_height_ins.set_sensitive(active);
            label_row_height.set_sensitive(active);
        };
        sync_row_height_sensitive();
        checkbutton_is_rich.signal_toggled().connect(sync_row_height_sensitive);
    }

    auto content_area = dialog.get_content_area();
    content_area->set_spacing(5);
    content_area->pack_start(grid);

    if (is_insert) {
        // Insert mode: show type selection checkboxes
        content_area->pack_start(checkbutton_is_light);
        content_area->pack_start(checkbutton_is_rich);
        content_area->pack_start(checkbutton_table_ins_from_file);
    } else if (not pTableStyle) {
        // Edit mode for light/heavy tables: show type selection
        content_area->pack_start(checkbutton_is_light);
        content_area->pack_start(checkbutton_is_rich);
    } else if (pTableStyle) {
        // Edit mode: show border / background style controls
        enum class PropScope { None = 0, Cell, Row, Column, Table };

        auto expandScope = [&](PropScope scope) -> std::set<std::pair<size_t,size_t>> {
            if (scope == PropScope::Table || scope == PropScope::None) return {};
            std::set<std::pair<size_t,size_t>> result;
            if (scope == PropScope::Cell) {
                result.emplace(currentRow, currentCol);
            } else if (scope == PropScope::Row) {
                for (size_t c = 0; c < numCols; ++c) result.emplace(currentRow, c);
            } else { // Column
                for (size_t r = 0; r < numRows; ++r) result.emplace(r, currentCol);
            }
            return result;
        };

        // Initialize border controls from current cell (or table defaults)
        auto bwIt = pTableStyle->cellBorderWidths.find({currentRow, currentCol});
        int initialBorderWidth = (bwIt != pTableStyle->cellBorderWidths.end()) ? bwIt->second : pTableStyle->borderWidth;

        auto bcIt = pTableStyle->cellBorderColors.find({currentRow, currentCol});
        std::string initialBorderColor = (bcIt != pTableStyle->cellBorderColors.end()) ? bcIt->second
            : (pTableStyle->borderColor.empty() ? "#000000" : pTableStyle->borderColor);

        auto adj_bw = Gtk::Adjustment::create(initialBorderWidth, 0, 10, 1);
        auto spinbutton_bw = Gtk::SpinButton{adj_bw};
        spinbutton_bw.set_value(initialBorderWidth);
        spinbutton_bw.set_sensitive(false);

        auto label_bw = Gtk::Label{_("Border Width")};
        label_bw.set_halign(Gtk::Align::ALIGN_START);
        label_bw.set_sensitive(false);

        auto label_bc = Gtk::Label{_("Border Color")};
        label_bc.set_halign(Gtk::Align::ALIGN_START);
        label_bc.set_sensitive(false);
        Gdk::RGBA borderRgba;
        borderRgba.set(initialBorderColor);
        auto colorbutton_border = Gtk::ColorButton{borderRgba};
        colorbutton_border.set_use_alpha(false);
        colorbutton_border.set_sensitive(false);

        // Initialize background controls from current cell (or table defaults)
        Gdk::RGBA bgRgba;
        auto bgIt = pTableStyle->cellBgColors.find({currentRow, currentCol});
        std::string initialBgColor = (bgIt != pTableStyle->cellBgColors.end()) ? bgIt->second : pTableStyle->tableBgColor;
        if (!initialBgColor.empty()) bgRgba.set(initialBgColor);
        else bgRgba.set("#ffffff");

        auto label_bgc = Gtk::Label{_("Background Color")};
        label_bgc.set_halign(Gtk::Align::ALIGN_START);
        label_bgc.set_sensitive(false);
        auto colorbutton_bg = Gtk::ColorButton{bgRgba};
        colorbutton_bg.set_use_alpha(false);
        colorbutton_bg.set_sensitive(false);
        auto button_clear_bg = Gtk::Button{_("Clear")};
        button_clear_bg.set_sensitive(false);

        // Border frame
        auto label_border_frame = Gtk::Label{std::string("<b>") + _("Border") + "</b>"};
        label_border_frame.set_use_markup();
        auto frame_border = Gtk::Frame{};
        frame_border.set_label_widget(label_border_frame);
        frame_border.set_margin_top(8);

        auto label_apply_border = Gtk::Label{_("Apply to:")};
        label_apply_border.set_halign(Gtk::Align::ALIGN_START);
        auto combo_scope_border = Gtk::ComboBoxText{};
        combo_scope_border.append(_("None"));
        combo_scope_border.append(_("Cell"));
        combo_scope_border.append(_("Row"));
        combo_scope_border.append(_("Column"));
        combo_scope_border.append(_("Table"));
        combo_scope_border.set_active(0);

        Gtk::Grid border_grid;
        border_grid.property_margin() = 6;
        border_grid.set_row_spacing(4);
        border_grid.set_column_spacing(8);
        border_grid.attach(label_apply_border,  0, 0, 1, 1);
        border_grid.attach(combo_scope_border,  1, 0, 3, 1);
        border_grid.attach(label_bw,            0, 1, 1, 1);
        border_grid.attach(spinbutton_bw,       1, 1, 1, 1);
        border_grid.attach(label_bc,            2, 1, 1, 1);
        border_grid.attach(colorbutton_border,  3, 1, 1, 1);
        frame_border.add(border_grid);

        // Background frame
        auto label_bg_frame = Gtk::Label{std::string("<b>") + _("Background") + "</b>"};
        label_bg_frame.set_use_markup();
        auto frame_bg = Gtk::Frame{};
        frame_bg.set_label_widget(label_bg_frame);
        frame_bg.set_margin_top(8);

        auto label_apply_bg = Gtk::Label{_("Apply to:")};
        label_apply_bg.set_halign(Gtk::Align::ALIGN_START);
        auto combo_scope_bg = Gtk::ComboBoxText{};
        combo_scope_bg.append(_("None"));
        combo_scope_bg.append(_("Cell"));
        combo_scope_bg.append(_("Row"));
        combo_scope_bg.append(_("Column"));
        combo_scope_bg.append(_("Table"));
        combo_scope_bg.set_active(0);

        Gtk::Grid bg_grid;
        bg_grid.property_margin() = 6;
        bg_grid.set_row_spacing(4);
        bg_grid.set_column_spacing(8);
        bg_grid.attach(label_apply_bg,   0, 0, 1, 1);
        bg_grid.attach(combo_scope_bg,   1, 0, 3, 1);
        bg_grid.attach(label_bgc,        0, 1, 1, 1);
        bg_grid.attach(colorbutton_bg,   1, 1, 1, 1);
        bg_grid.attach(button_clear_bg,  2, 1, 1, 1);
        frame_bg.add(bg_grid);

        // ── Column Width frame ────────────────────────────────────────────────
        auto label_colw_frame = Gtk::Label{std::string("<b>") + _("Column Width") + "</b>"};
        label_colw_frame.set_use_markup();
        auto frame_colw = Gtk::Frame{};
        frame_colw.set_label_widget(label_colw_frame);
        frame_colw.set_margin_top(8);

        auto label_apply_colw = Gtk::Label{_("Apply to:")};
        label_apply_colw.set_halign(Gtk::Align::ALIGN_START);
        auto combo_scope_colw = Gtk::ComboBoxText{};
        combo_scope_colw.append(_("None"));
        combo_scope_colw.append(_("Column"));
        combo_scope_colw.append(_("Table"));
        combo_scope_colw.set_active(0);

        auto label_colw_val = Gtk::Label{_("Min Width")};
        label_colw_val.set_halign(Gtk::Align::ALIGN_START);
        label_colw_val.set_sensitive(false);
        const int initColW = (currentColWidth > 0) ? currentColWidth : pCtConfig->tableColWidthDefault;
        auto adj_colw_new = Gtk::Adjustment::create(initColW, 1, 10000, 1);
        auto spinbutton_colw_new = Gtk::SpinButton{adj_colw_new};
        spinbutton_colw_new.set_value(initColW);
        spinbutton_colw_new.set_sensitive(false);

        Gtk::Grid colw_grid;
        colw_grid.property_margin() = 6;
        colw_grid.set_row_spacing(4);
        colw_grid.set_column_spacing(8);
        colw_grid.attach(label_apply_colw,   0, 0, 1, 1);
        colw_grid.attach(combo_scope_colw,   1, 0, 2, 1);
        colw_grid.attach(label_colw_val,     0, 1, 1, 1);
        colw_grid.attach(spinbutton_colw_new,1, 1, 1, 1);
        frame_colw.add(colw_grid);

        combo_scope_colw.signal_changed().connect([&](){
            const bool active = combo_scope_colw.get_active_row_number() != 0;
            spinbutton_colw_new.set_sensitive(active);
            label_colw_val.set_sensitive(active);
        });

        // ── Row Height frame ──────────────────────────────────────────────────
        auto label_rh_frame = Gtk::Label{std::string("<b>") + _("Row Height") + "</b>"};
        label_rh_frame.set_use_markup();
        auto frame_rh = Gtk::Frame{};
        frame_rh.set_label_widget(label_rh_frame);
        frame_rh.set_margin_top(8);

        auto label_apply_rh = Gtk::Label{_("Apply to:")};
        label_apply_rh.set_halign(Gtk::Align::ALIGN_START);
        auto combo_scope_rh = Gtk::ComboBoxText{};
        combo_scope_rh.append(_("None"));
        combo_scope_rh.append(_("Row"));
        combo_scope_rh.append(_("Table"));
        combo_scope_rh.set_active(0);

        auto label_rh_val = Gtk::Label{_("Min Height")};
        label_rh_val.set_halign(Gtk::Align::ALIGN_START);
        label_rh_val.set_sensitive(false);
        auto rhIt = pTableStyle->rowMinHeights.find(currentRow);
        const int initRH = (rhIt != pTableStyle->rowMinHeights.end()) ? rhIt->second : pTableStyle->rowMinHeightDefault;
        auto adj_rh = Gtk::Adjustment::create(initRH, 0, 2000, 1);
        auto spinbutton_rh = Gtk::SpinButton{adj_rh};
        spinbutton_rh.set_value(initRH);
        spinbutton_rh.set_sensitive(false);

        Gtk::Grid rh_grid;
        rh_grid.property_margin() = 6;
        rh_grid.set_row_spacing(4);
        rh_grid.set_column_spacing(8);
        rh_grid.attach(label_apply_rh,  0, 0, 1, 1);
        rh_grid.attach(combo_scope_rh,  1, 0, 2, 1);
        rh_grid.attach(label_rh_val,    0, 1, 1, 1);
        rh_grid.attach(spinbutton_rh,   1, 1, 1, 1);
        frame_rh.add(rh_grid);

        combo_scope_rh.signal_changed().connect([&](){
            const bool active = combo_scope_rh.get_active_row_number() != 0;
            spinbutton_rh.set_sensitive(active);
            label_rh_val.set_sensitive(active);
        });

        // ── Alignment frame ───────────────────────────────────────────────────
        auto label_align_frame = Gtk::Label{std::string("<b>") + _("Text Alignment") + "</b>"};
        label_align_frame.set_use_markup();
        auto frame_align = Gtk::Frame{};
        frame_align.set_label_widget(label_align_frame);
        frame_align.set_margin_top(8);

        auto label_apply_align = Gtk::Label{_("Apply to:")};
        label_apply_align.set_halign(Gtk::Align::ALIGN_START);
        auto combo_scope_align = Gtk::ComboBoxText{};
        combo_scope_align.append(_("None"));
        combo_scope_align.append(_("Cell"));
        combo_scope_align.append(_("Row"));
        combo_scope_align.append(_("Column"));
        combo_scope_align.append(_("Table"));
        combo_scope_align.set_active(0);

        auto label_halign = Gtk::Label{_("Horizontal:")};
        label_halign.set_halign(Gtk::Align::ALIGN_START);
        label_halign.set_sensitive(false);
        auto combo_halign = Gtk::ComboBoxText{};
        combo_halign.append(_("Left"));
        combo_halign.append(_("Center"));
        combo_halign.append(_("Right"));
        combo_halign.set_sensitive(false);
        {
            const auto haIt = pTableStyle->cellHAlign.find({currentRow, currentCol});
            const std::string& ha = (haIt != pTableStyle->cellHAlign.end()) ? haIt->second : pTableStyle->tableHAlignDefault;
            if      (ha == "center") combo_halign.set_active(1);
            else if (ha == "right")  combo_halign.set_active(2);
            else                     combo_halign.set_active(0);
        }

        auto label_valign = Gtk::Label{_("Vertical:")};
        label_valign.set_halign(Gtk::Align::ALIGN_START);
        label_valign.set_sensitive(false);
        auto combo_valign = Gtk::ComboBoxText{};
        combo_valign.append(_("Top"));
        combo_valign.append(_("Middle"));
        combo_valign.append(_("Bottom"));
        combo_valign.set_sensitive(false);
        {
            const auto vaIt = pTableStyle->cellVAlign.find({currentRow, currentCol});
            const std::string& va = (vaIt != pTableStyle->cellVAlign.end()) ? vaIt->second : pTableStyle->tableVAlignDefault;
            if      (va == "middle") combo_valign.set_active(1);
            else if (va == "bottom") combo_valign.set_active(2);
            else                     combo_valign.set_active(0);
        }

        Gtk::Grid align_grid;
        align_grid.property_margin() = 6;
        align_grid.set_row_spacing(4);
        align_grid.set_column_spacing(8);
        align_grid.attach(label_apply_align, 0, 0, 1, 1);
        align_grid.attach(combo_scope_align, 1, 0, 3, 1);
        align_grid.attach(label_halign,      0, 1, 1, 1);
        align_grid.attach(combo_halign,      1, 1, 1, 1);
        align_grid.attach(label_valign,      2, 1, 1, 1);
        align_grid.attach(combo_valign,      3, 1, 1, 1);
        frame_align.add(align_grid);

        combo_scope_align.signal_changed().connect([&](){
            const bool active = combo_scope_align.get_active_row_number() != 0;
            combo_halign.set_sensitive(active);
            combo_valign.set_sensitive(active);
            label_halign.set_sensitive(active);
            label_valign.set_sensitive(active);
        });

        // ── Text Reflow frame ─────────────────────────────────────────────────
        auto label_wrap_frame = Gtk::Label{std::string("<b>") + _("Text Reflow") + "</b>"};
        label_wrap_frame.set_use_markup();
        auto frame_wrap = Gtk::Frame{};
        frame_wrap.set_label_widget(label_wrap_frame);
        frame_wrap.set_margin_top(8);

        auto label_apply_wrap = Gtk::Label{_("Apply to:")};
        label_apply_wrap.set_halign(Gtk::Align::ALIGN_START);
        auto combo_scope_wrap = Gtk::ComboBoxText{};
        combo_scope_wrap.append(_("None"));
        combo_scope_wrap.append(_("Cell"));
        combo_scope_wrap.append(_("Row"));
        combo_scope_wrap.append(_("Column"));
        combo_scope_wrap.append(_("Table"));
        combo_scope_wrap.set_active(0);

        auto checkbutton_wrap = Gtk::CheckButton(_("Wrap text"));
        checkbutton_wrap.set_sensitive(false);
        {
            bool initWrap;
            const auto wIt = pTableStyle->cellWrap.find({currentRow, currentCol});
            if (wIt != pTableStyle->cellWrap.end()) initWrap = wIt->second;
            else if (pTableStyle->tableWrapDefaultSet) initWrap = pTableStyle->tableWrapDefault;
            else initWrap = pCtMainWin->get_ct_config()->lineWrapping;
            checkbutton_wrap.set_active(initWrap);
        }

        Gtk::Grid wrap_grid;
        wrap_grid.property_margin() = 6;
        wrap_grid.set_row_spacing(4);
        wrap_grid.set_column_spacing(8);
        wrap_grid.attach(label_apply_wrap,  0, 0, 1, 1);
        wrap_grid.attach(combo_scope_wrap,  1, 0, 3, 1);
        wrap_grid.attach(checkbutton_wrap,  0, 1, 4, 1);
        frame_wrap.add(wrap_grid);

        combo_scope_wrap.signal_changed().connect([&](){
            checkbutton_wrap.set_sensitive(combo_scope_wrap.get_active_row_number() != 0);
        });

        content_area->pack_start(frame_colw);
        content_area->pack_start(frame_rh);
        content_area->pack_start(frame_align);
        content_area->pack_start(frame_wrap);
        content_area->pack_start(frame_border);
        content_area->pack_start(frame_bg);
        content_area->show_all();

        auto borderScopeFromCombo = [&]() -> PropScope {
            return static_cast<PropScope>(combo_scope_border.get_active_row_number());
        };
        auto bgScopeFromCombo = [&]() -> PropScope {
            return static_cast<PropScope>(combo_scope_bg.get_active_row_number());
        };

        combo_scope_border.signal_changed().connect([&](){
            const bool active = borderScopeFromCombo() != PropScope::None;
            spinbutton_bw.set_sensitive(active);
            colorbutton_border.set_sensitive(active);
            label_bw.set_sensitive(active);
            label_bc.set_sensitive(active);
        });
        combo_scope_bg.signal_changed().connect([&](){
            const bool active = bgScopeFromCombo() != PropScope::None;
            colorbutton_bg.set_sensitive(active);
            button_clear_bg.set_sensitive(active);
            label_bgc.set_sensitive(active);
        });

        bool bgCleared{false};
        button_clear_bg.signal_clicked().connect([&](){
            Gdk::RGBA white; white.set("#ffffff");
            colorbutton_bg.set_rgba(white);
            bgCleared = true;
        });

        auto on_key_press_dialog2 = [&](GdkEventKey* pEventKey)->bool{
            if (GDK_KEY_Return == pEventKey->keyval or GDK_KEY_KP_Enter == pEventKey->keyval) {
                Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_ACCEPT));
                pButton->grab_focus();
                pButton->clicked();
                return true;
            }
            if (GDK_KEY_Escape == pEventKey->keyval) {
                Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_REJECT));
                pButton->grab_focus();
                pButton->clicked();
                return true;
            }
            return false;
        };
        dialog.signal_key_press_event().connect(on_key_press_dialog2, false);

        const int oldColWidthDefault = pCtConfig->tableColWidthDefault;
        const auto resp2 = dialog.run();
        pCtConfig->tableColWidthDefault = spinbutton_col_width.get_value_as_int();
        if (Gtk::RESPONSE_ACCEPT == resp2) {
            const bool colWidthChanged = (pCtConfig->tableColWidthDefault != oldColWidthDefault);
            const auto borderScope = borderScopeFromCombo();
            if (borderScope != PropScope::None) {
                const int newBW = spinbutton_bw.get_value_as_int();
                auto bc = colorbutton_border.get_rgba();
                char hex[8];
                snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                         (int)(bc.get_red()*255), (int)(bc.get_green()*255), (int)(bc.get_blue()*255));
                const std::string newBC{hex};
                if (borderScope == PropScope::Table) {
                    pTableStyle->borderWidth = newBW;
                    pTableStyle->borderColor = newBC;
                    pTableStyle->cellBorderWidths.clear();
                    pTableStyle->cellBorderColors.clear();
                    pTableStyle->cellBorderSeq.clear();
                } else {
                    ++pTableStyle->borderSeqCounter;
                    for (const auto& cell : expandScope(borderScope)) {
                        pTableStyle->cellBorderWidths[cell] = newBW;
                        pTableStyle->cellBorderColors[cell] = newBC;
                        pTableStyle->cellBorderSeq[cell] = pTableStyle->borderSeqCounter;
                    }
                }
            }

            const auto bgScope = bgScopeFromCombo();
            if (bgScope != PropScope::None) {
                auto bg = colorbutton_bg.get_rgba();
                char bhex[8];
                snprintf(bhex, sizeof(bhex), "#%02x%02x%02x",
                         (int)(bg.get_red()*255), (int)(bg.get_green()*255), (int)(bg.get_blue()*255));
                if (bgScope == PropScope::Table) {
                    pTableStyle->cellBgColors.clear();
                    if (bgCleared) pTableStyle->tableBgColor.clear();
                    else pTableStyle->tableBgColor = bhex;
                } else {
                    const auto cells = expandScope(bgScope);
                    if (bgCleared) {
                        for (const auto& cell : cells) pTableStyle->cellBgColors.erase(cell);
                    } else {
                        for (const auto& cell : cells) pTableStyle->cellBgColors[cell] = bhex;
                    }
                }
            }

            // Column width
            const int colwScopeIdx = combo_scope_colw.get_active_row_number();
            if (colwScopeIdx != 0) {
                const int newW = spinbutton_colw_new.get_value_as_int();
                pTableStyle->pendingColWidthVal = newW;
                pTableStyle->pendingColWidthTable = (colwScopeIdx == 2); // 2 = Table
                pTableStyle->pendingColWidthIdx = currentCol;
            }

            // Row height
            const int rhScopeIdx = combo_scope_rh.get_active_row_number();
            if (rhScopeIdx != 0) {
                const int newH = spinbutton_rh.get_value_as_int();
                if (rhScopeIdx == 2) { // Table
                    pTableStyle->rowMinHeightDefault = newH;
                    pTableStyle->rowMinHeights.clear();
                } else { // Row
                    pTableStyle->rowMinHeights[currentRow] = newH;
                }
            }

            // Alignment
            const int alignScopeIdx = combo_scope_align.get_active_row_number();
            if (alignScopeIdx != 0) {
                static const char* hAlignVals[] = {"left", "center", "right"};
                static const char* vAlignVals[] = {"top", "middle", "bottom"};
                const std::string newHA = hAlignVals[combo_halign.get_active_row_number()];
                const std::string newVA = vAlignVals[combo_valign.get_active_row_number()];
                if (alignScopeIdx == 4) { // Table
                    pTableStyle->tableHAlignDefault = (newHA == "left") ? "" : newHA;
                    pTableStyle->tableVAlignDefault = (newVA == "top") ? "" : newVA;
                    pTableStyle->cellHAlign.clear();
                    pTableStyle->cellVAlign.clear();
                } else {
                    for (const auto& cell : expandScope(static_cast<PropScope>(alignScopeIdx))) {
                        pTableStyle->cellHAlign[cell] = newHA;
                        pTableStyle->cellVAlign[cell] = newVA;
                    }
                }
            }

            // Wrap
            const int wrapScopeIdx = combo_scope_wrap.get_active_row_number();
            if (wrapScopeIdx != 0) {
                const bool newWrap = checkbutton_wrap.get_active();
                if (wrapScopeIdx == 4) { // Table
                    pTableStyle->tableWrapDefault = newWrap;
                    pTableStyle->tableWrapDefaultSet = true;
                    pTableStyle->cellWrap.clear();
                } else {
                    for (const auto& cell : expandScope(static_cast<PropScope>(wrapScopeIdx))) {
                        pTableStyle->cellWrap[cell] = newWrap;
                    }
                }
            }

            if (borderScope != PropScope::None || bgScope != PropScope::None || colWidthChanged ||
                colwScopeIdx != 0 || rhScopeIdx != 0 || alignScopeIdx != 0 || wrapScopeIdx != 0) {
                return TableHandleResp::Ok;
            }
            return TableHandleResp::Cancel;
        }
        return TableHandleResp::Cancel;
    }

    content_area->show_all();

    // Rich and lightweight are mutually exclusive
    checkbutton_is_rich.signal_toggled().connect([&](){
        if (checkbutton_is_rich.get_active()) {
            checkbutton_is_light.set_active(false);
            checkbutton_is_light.set_sensitive(false);
        } else {
            checkbutton_is_light.set_sensitive(true);
        }
    });
    checkbutton_is_light.signal_toggled().connect([&](){
        if (checkbutton_is_light.get_active()) {
            checkbutton_is_rich.set_active(false);
            checkbutton_is_rich.set_sensitive(false);
        } else {
            checkbutton_is_rich.set_sensitive(true);
        }
    });

    checkbutton_table_ins_from_file.signal_toggled().connect([&](){
        const bool from_file = checkbutton_table_ins_from_file.get_active();
        grid.set_sensitive(not from_file);
        checkbutton_is_rich.set_sensitive(not from_file and not checkbutton_is_light.get_active());
    });

    if (is_insert) {
        auto f_reeval_is_light = [&](){
            // Only auto-set lightweight if rich is not chosen
            if (not checkbutton_is_rich.get_active()) {
                checkbutton_is_light.set_active(spinbutton_rows.get_value_as_int()*spinbutton_columns.get_value_as_int() > pCtConfig->tableCellsGoLight);
            }
        };
        spinbutton_rows.signal_value_changed().connect([f_reeval_is_light](){ f_reeval_is_light(); });
        spinbutton_columns.signal_value_changed().connect([f_reeval_is_light](){ f_reeval_is_light(); });
    }

    auto on_key_press_dialog = [&](GdkEventKey* pEventKey)->bool{
        if (GDK_KEY_Return == pEventKey->keyval or GDK_KEY_KP_Enter == pEventKey->keyval) {
            Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_ACCEPT));
            pButton->grab_focus();
            pButton->clicked();
            return true;
        }
        if (GDK_KEY_Escape == pEventKey->keyval) {
            Gtk::Button* pButton = static_cast<Gtk::Button*>(dialog.get_widget_for_response(Gtk::RESPONSE_REJECT));
            pButton->grab_focus();
            pButton->clicked();
            return true;
        }
        return false;
    };
    dialog.signal_key_press_event().connect(on_key_press_dialog, false/*call me before other*/);

    const auto resp = dialog.run();
    if (Gtk::RESPONSE_ACCEPT == resp) {
        is_rich = checkbutton_is_rich.get_active();
        is_light = not is_rich and checkbutton_is_light.get_active();
        pCtConfig->tableRows = spinbutton_rows.get_value_as_int();
        pCtConfig->tableColumns = spinbutton_columns.get_value_as_int();
        pCtConfig->tableColWidthDefault = spinbutton_col_width.get_value_as_int();
        if (is_insert) {
            pCtConfig->tableRowHeightDefault = spinbutton_row_height_ins.get_value_as_int();
        }
        if (checkbutton_table_ins_from_file.get_active()) {
            return TableHandleResp::OkFromFile;
        }
        return TableHandleResp::Ok;
    }
    return TableHandleResp::Cancel;
}
#else
// GTK4 minimal fallbacks to satisfy build; functionality to be implemented.
Glib::ustring CtDialogs::latex_handle_dialog(CtMainWin* pCtMainWin,
                                             const Glib::ustring& latex_text)
{
    (void)pCtMainWin;
    return latex_text;
}

Glib::RefPtr<Gdk::Pixbuf> CtDialogs::image_handle_dialog(Gtk::Window& parent_win,
                                                         Glib::RefPtr<Gdk::Pixbuf> rOriginalPixbuf,
                                                         Glib::RefPtr<Gdk::Pixbuf> rHighResPixbuf)
{
    (void)parent_win;
    (void)rHighResPixbuf;
    return rOriginalPixbuf;
}

bool CtDialogs::codeboxhandle_dialog(CtMainWin* pCtMainWin,
                                     const Glib::ustring& title)
{
    (void)pCtMainWin; (void)title;
    return false;
}

CtDialogs::TableHandleResp CtDialogs::table_handle_dialog(CtMainWin* pCtMainWin,
                                                          const Glib::ustring& title,
                                                          const bool is_insert,
                                                          bool& is_light,
                                                          bool& is_rich,
                                                          CtTableStyle* pTableStyle,
                                                          size_t currentRow,
                                                          size_t currentCol,
                                                          size_t numRows,
                                                          size_t numCols,
                                                          int currentColWidth)
{
    (void)pCtMainWin; (void)title; (void)is_insert; (void)is_light; (void)is_rich; (void)pTableStyle;
    (void)currentRow; (void)currentCol; (void)numRows; (void)numCols; (void)currentColWidth;
    return TableHandleResp::Cancel;
}
#endif
