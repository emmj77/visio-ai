#include "perceive/windows.h"

#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstring>

namespace visio {

namespace {

// Lit une propriété et rend son contenu (32 bits) ; vide si absente.
std::vector<long> prop_32(Display* d, Window w, Atom prop) {
    std::vector<long> out;
    Atom type = None;
    int format = 0;
    unsigned long n = 0, reste = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, prop, 0, 4096, False, AnyPropertyType,
                           &type, &format, &n, &reste, &data) != Success || !data)
        return out;
    if (format == 32) {
        const long* p = reinterpret_cast<const long*>(data);
        out.assign(p, p + n);
    }
    XFree(data);
    return out;
}

bool est_viewable(Display* d, Window w) {
    XWindowAttributes a{};
    if (!XGetWindowAttributes(d, w, &a)) return false;
    return a.map_state == IsViewable;
}

} // namespace

Rect window_rect(Display* d, Window w) {
    XWindowAttributes a{};
    if (!XGetWindowAttributes(d, w, &a)) return {};
    int rx = 0, ry = 0;
    Window enfant = None;
    if (!XTranslateCoordinates(d, w, DefaultRootWindow(d), 0, 0, &rx, &ry, &enfant)) return {};
    return Rect{rx, ry, a.width, a.height};
}

std::string window_title(Display* d, Window w) {
    const Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
    const Atom net_wm_name = XInternAtom(d, "_NET_WM_NAME", False);
    std::string t;
    Atom type = None;
    int format = 0;
    unsigned long n = 0, reste = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, net_wm_name, 0, 4096, False, utf8,
                           &type, &format, &n, &reste, &data) == Success && data) {
        t.assign(reinterpret_cast<const char*>(data), n);
        XFree(data);
    }
    if (t.empty()) {
        char* nom = nullptr;
        if (XFetchName(d, w, &nom) && nom) {
            t = nom;
            XFree(nom);
        }
    }
    return t;
}

std::string window_class(Display* d, Window w) {
    XClassHint h{};
    if (XGetClassHint(d, w, &h)) {
        std::string c = h.res_class ? h.res_class : "";
        if (h.res_name) XFree(h.res_name);
        if (h.res_class) XFree(h.res_class);
        for (auto& ch : c) ch = (char)tolower((unsigned char)ch);
        return c;
    }
    return {};
}

std::vector<WindowInfo> list_windows(Display* d, bool ordre_pile) {
    std::vector<WindowInfo> out;
    const Window root = DefaultRootWindow(d);
    const Atom prop = XInternAtom(d, ordre_pile ? "_NET_CLIENT_LIST_STACKING" : "_NET_CLIENT_LIST",
                                  False);

    std::vector<long> ids = prop_32(d, root, prop);

    if (ids.empty()) {
        // Pas de WM (ou pas d'EWMH) : on prend les enfants directs de la racine.
        Window r = None, parent = None, *enfants = nullptr;
        unsigned int n = 0;
        if (XQueryTree(d, root, &r, &parent, &enfants, &n)) {
            for (unsigned int i = 0; i < n; ++i) ids.push_back((long)enfants[i]);
            if (enfants) XFree(enfants);
        }
    }

    out.reserve(ids.size());
    for (long id : ids) {
        const Window w = (Window)id;
        WindowInfo wi;
        wi.id = w;
        wi.viewable = est_viewable(d, w);
        wi.rect = window_rect(d, w);
        wi.title = window_title(d, w);
        wi.cls = window_class(d, w);
        out.push_back(std::move(wi));
    }
    return out;
}

Window window_at(const std::vector<WindowInfo>& pile, int x, int y) {
    for (auto it = pile.rbegin(); it != pile.rend(); ++it)
        if (it->viewable && it->rect.contains(x, y)) return it->id;
    return None;
}

} // namespace visio
