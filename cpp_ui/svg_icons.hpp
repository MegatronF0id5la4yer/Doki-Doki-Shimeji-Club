#pragma once

#include <cairo/cairo.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <string>
#include <unordered_map>

/**
 * Loads, caches, and tints crisp vector SVG icons via librsvg + Cairo.
 * Keeps the entire interface strictly free of Unicode emojis.
 */
class SvgIconCache {
public:
    explicit SvgIconCache(std::string icons_dir) : icons_dir_(std::move(icons_dir)) {}

    ~SvgIconCache() {
        for (auto& kv : handles_) {
            if (kv.second) {
                g_object_unref(kv.second);
            }
        }
        handles_.clear();
    }

    SvgIconCache(const SvgIconCache&) = delete;
    SvgIconCache& operator=(const SvgIconCache&) = delete;

    RsvgHandle* get(const std::string& name) {
        auto it = handles_.find(name);
        if (it != handles_.end()) {
            return it->second;
        }
        std::string path = icons_dir_ + "/" + name + ".svg";
        GError* err = nullptr;
        RsvgHandle* h = rsvg_handle_new_from_file(path.c_str(), &err);
        if (!h) {
            if (err) {
                g_printerr("[svg] failed loading %s: %s\n", path.c_str(), err->message);
                g_error_free(err);
            }
            handles_[name] = nullptr;
            return nullptr;
        }
        handles_[name] = h;
        return h;
    }

    /** Paints a monochrome SVG icon tinted with (r, g, b, a) via Cairo alpha mask. */
    void paint_tinted(
        cairo_t* cr,
        const std::string& name,
        double x,
        double y,
        double size,
        double r,
        double g,
        double b,
        double a = 1.0
    ) {
        RsvgHandle* h = get(name);
        if (!h) {
            return;
        }
        cairo_surface_t* mask = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, static_cast<int>(size), static_cast<int>(size));
        cairo_t* mcr = cairo_create(mask);
        RsvgRectangle vp{0.0, 0.0, size, size};
        GError* err = nullptr;
        if (!rsvg_handle_render_document(h, mcr, &vp, &err) && err) {
            g_error_free(err);
        }
        cairo_destroy(mcr);

        cairo_save(cr);
        cairo_set_source_rgba(cr, r, g, b, a);
        cairo_mask_surface(cr, mask, x, y);
        cairo_restore(cr);
        cairo_surface_destroy(mask);
    }

    /** Renders a tinted GdkPixbuf from an SVG icon (caller owns reference). */
    GdkPixbuf* make_pixbuf(
        const std::string& name,
        int size,
        double r,
        double g,
        double b,
        double a = 1.0
    ) {
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
        cairo_t* cr = cairo_create(surf);
        paint_tinted(cr, name, 0.0, 0.0, static_cast<double>(size), r, g, b, a);
        cairo_destroy(cr);

        GdkPixbuf* pb = gdk_pixbuf_get_from_surface(surf, 0, 0, size, size);
        cairo_surface_destroy(surf);
        return pb;
    }

    /** Builds a tinted GtkImage widget from an SVG icon for GTK buttons and headers. */
    GtkWidget* make_gtk_image(
        const std::string& name,
        int size,
        double r,
        double g,
        double b,
        double a = 1.0
    ) {
        GdkPixbuf* pb = make_pixbuf(name, size, r, g, b, a);
        GtkWidget* img = gtk_image_new_from_pixbuf(pb);
        if (pb) {
            g_object_unref(pb);
        }
        return img;
    }

private:
    std::string icons_dir_;
    std::unordered_map<std::string, RsvgHandle*> handles_;
};

