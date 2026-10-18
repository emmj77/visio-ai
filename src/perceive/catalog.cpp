#include "perceive/catalog.h"

#include <algorithm>
#include <cstdio>

namespace visio {

const char* nom_genre(Genre g) {
    switch (g) {
        case Genre::Fenetre: return "fenetre";
        case Genre::Panneau: return "panneau";
        case Genre::Barre:   return "barre";
        case Genre::Bouton:  return "bouton";
        case Genre::Champ:   return "champ";
        case Genre::Texte:   return "texte";
        default:             return "?";
    }
}

uint64_t identifiant(Window fenetre, Genre genre, const Rect& r) {
    uint64_t h = 1469598103934665603ull;   // FNV-1a
    const auto melange = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    melange((uint64_t)fenetre);
    melange((uint64_t)genre);
    melange((uint64_t)(uint32_t)r.x);
    melange((uint64_t)(uint32_t)r.y);
    melange((uint64_t)(uint32_t)r.w);
    melange((uint64_t)(uint32_t)r.h);
    return h;
}

Genre deviner_genre(const Rect& r, const Rect& fenetre, double remplissage) {
    if (r.area() <= 0) return Genre::Inconnu;
    const double part = (double)r.area() / (double)std::max<int64_t>(1, fenetre.area());
    if (r == fenetre || part > 0.50) return Genre::Panneau;
    if (r.h <= 24) return Genre::Texte;                       // une ligne de texte
    if (r.h <= 44 && r.w > 4 * r.h) return Genre::Barre;      // ruban horizontal
    if (r.w <= 260 && r.h <= 64 && r.w >= 12) {
        const double ratio = (double)r.w / (double)r.h;
        if (ratio >= 1.0 && ratio <= 8.0 && remplissage >= 0.75) return Genre::Bouton;
        if (r.h > 40 && ratio < 6.0) return Genre::Champ;     // zone de saisie
    }
    if (r.w > 260 && r.h > 44) return Genre::Panneau;
    return Genre::Inconnu;
}

const std::vector<Element>& Catalogue::maj(Window fenetre, const std::vector<Region>& regions) {
    courants_.clear();
    glyphes_ecartes = 0;

    // la fenêtre elle-même, en premier
    {
        Element e;
        e.fenetre = fenetre;
        e.genre = Genre::Fenetre;
        for (const auto& r : regions)
            if (e.rect.empty() || r.rect.area() > e.rect.area()) e.rect = r.rect;
        if (!e.rect.empty()) {
            e.id = identifiant(fenetre, e.genre, e.rect);
            courants_.push_back(e);
        }
    }

    const Rect cadre = courants_.empty() ? Rect{} : courants_.front().rect;

    for (const auto& r : regions) {
        // Un filet de 1 à 4 px, c'est un séparateur ou une barre de défilement : utile,
        // mais pas une cible de clic. La fenêtre, elle, est déjà décrite plus haut.
        if (r.rect.w < 6 || r.rect.h < 6) continue;
        if (r.rect == cadre) continue;
        if (r.rect.w <= 16 && r.rect.h <= 22) { ++glyphes_ecartes; continue; }

        Element e;
        e.fenetre = fenetre;
        e.rect = r.rect;
        e.couleur = r.couleur;
        e.pixels = r.pixels;
        e.genre = deviner_genre(r.rect, cadre, r.remplissage);
        e.id = identifiant(fenetre, e.genre, e.rect);
        courants_.push_back(e);
    }

    // tri : la fenêtre d'abord, puis haut -> bas et gauche -> droite
    std::sort(courants_.begin(), courants_.end(), [](const Element& a, const Element& b) {
        if (a.genre == Genre::Fenetre) return b.genre != Genre::Fenetre;
        if (b.genre == Genre::Fenetre) return false;
        if (a.rect.y != b.rect.y) return a.rect.y < b.rect.y;
        return a.rect.x < b.rect.x;
    });

    // `seq` est la séquence de NAISSANCE de l'élément, pas un compteur de versions :
    // il ne bouge plus une fois l'élément connu. C'est ce qui permet au modèle de dire
    // « le bouton que je voyais au tour 12 » et de le retrouver.
    std::vector<uint64_t> vus;
    vus.reserve(courants_.size());
    for (auto& e : courants_) {
        vus.push_back(e.id);
        auto it = elements_.find(e.id);
        if (it == elements_.end()) {
            e.seq = seq_++;
            elements_[e.id] = e;
        } else {
            const int naissance = it->second.seq;   // lire AVANT d'écraser
            it->second = e;
            it->second.seq = naissance;
            e.seq = naissance;
        }
    }

    // Ce qui a disparu de la fenêtre disparaît du catalogue : sinon un « sous_point »
    // finirait par désigner un bouton qui n'existe plus.
    for (auto it = elements_.begin(); it != elements_.end();) {
        if (it->second.fenetre == fenetre &&
            std::find(vus.begin(), vus.end(), it->first) == vus.end())
            it = elements_.erase(it);
        else
            ++it;
    }

    return courants_;
}

const Element* Catalogue::par_id(uint64_t id) const {
    auto it = elements_.find(id);
    return it == elements_.end() ? nullptr : &it->second;
}

const Element* Catalogue::sous_point(int x, int y) const {
    const Element* best = nullptr;
    for (const auto& [id, e] : elements_) {
        (void)id;
        if (!e.rect.contains(x, y)) continue;
        if (!best || e.rect.area() < best->rect.area()) best = &e;
    }
    return best;
}

std::string Catalogue::texte() const {
    std::string out;
    char ligne[192];
    for (const auto& e : courants_) {
        std::snprintf(ligne, sizeof ligne, "%016llx %-8s %4dx%-4d @ %4d,%-4d #%06x\n",
                      (unsigned long long)e.id, nom_genre(e.genre), e.rect.w, e.rect.h,
                      e.rect.x, e.rect.y, e.couleur & 0xFFFFFF);
        out += ligne;
    }
    return out;
}

void Catalogue::oublier_fenetre(Window w) {
    for (auto it = elements_.begin(); it != elements_.end();)
        it = (it->second.fenetre == w) ? elements_.erase(it) : std::next(it);
    courants_.erase(std::remove_if(courants_.begin(), courants_.end(),
                                   [w](const Element& e) { return e.fenetre == w; }),
                    courants_.end());
}

void Catalogue::vider() {
    elements_.clear();
    courants_.clear();
}

} // namespace visio
