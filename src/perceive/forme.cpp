// src/perceive/forme.cpp — modeliser une forme, puis la dessiner depuis son modele.
#include "perceive/forme.h"

#include "common/rect.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string>

namespace visio {
namespace {

constexpr double PI = 3.14159265358979323846;

double distance_segment(const Point& p, const Point& a, const Point& b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double l2 = dx * dx + dy * dy;
    if (l2 < 1e-12) return std::hypot(p.x - a.x, p.y - a.y);
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2;
    t = std::max(0.0, std::min(1.0, t));
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Un COIN est un endroit ou le contour tourne franchement. Un rond n'en a aucun, un triangle
// en a trois : c'est ce qui distingue « pointe » de « chapeau », et non une largeur mesuree.
std::vector<size_t> indices_coins(const std::vector<std::pair<double, double>>& c, int k,
                                  double seuil_deg) {
    std::vector<size_t> coins;
    const size_t n = c.size();
    if (n < (size_t)(4 * k)) return coins;
    const double seuil = std::cos(seuil_deg * PI / 180.0);
    std::vector<char> marque(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const auto& a = c[(i + n - k) % n];
        const auto& b = c[i];
        const auto& d = c[(i + k) % n];
        const double x1 = b.first - a.first, y1 = b.second - a.second;
        const double x2 = d.first - b.first, y2 = d.second - b.second;
        const double n1 = std::hypot(x1, y1), n2 = std::hypot(x2, y2);
        if (n1 < 1e-9 || n2 < 1e-9) continue;
        const double cosinus = (x1 * x2 + y1 * y2) / (n1 * n2);
        if (cosinus < seuil) marque[i] = 1;
    }
    // Regrouper les indices voisins : un coin occupe plusieurs echantillons.
    for (size_t i = 0; i < n; ++i) {
        if (!marque[i]) continue;
        if (i && marque[(i + n - 1) % n]) continue;   // pas le debut d'un groupe
        size_t j = i, len = 0;
        Point somme{};
        while (j < n && marque[j]) { somme.x += c[j].first; somme.y += c[j].second; ++j; ++len; }
        if (len) coins.push_back(((i + j) / 2) % n);
    }
    return coins;
}

} // namespace

Forme analyser_forme(const std::vector<std::pair<double, double>>& contour) {
    Forme f;
    const size_t n = contour.size();
    if (n < 12) return f;

    double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
    Point centre{};
    for (const auto& p : contour) {
        x0 = std::min(x0, p.first); x1 = std::max(x1, p.first);
        y0 = std::min(y0, p.second); y1 = std::max(y1, p.second);
        centre.x += p.first; centre.y += p.second;
    }
    centre.x /= (double)n; centre.y /= (double)n;

    // --- ellipse : centre au milieu de la boite, demi-axes de la boite
    Forme e;
    e.genre = Forme::Genre::Ellipse;
    e.cx = (x0 + x1) / 2; e.cy = (y0 + y1) / 2;
    e.a = (x1 - x0) / 2;  e.b = (y1 - y0) / 2;
    double err_e = 0;
    for (const auto& p : contour) {
        const double u = (p.first - e.cx) / std::max(1e-9, e.a);
        const double v = (p.second - e.cy) / std::max(1e-9, e.b);
        err_e += std::abs(std::sqrt(u * u + v * v) - 1.0);
    }
    e.erreur = err_e / (double)n * (e.a + e.b) / 2;   // en pixels

    // --- polygone : les coins, dans l'ordre du contour
    const std::vector<size_t> ic = indices_coins(contour, 4, 32.0);
    Forme p;
    p.genre = Forme::Genre::Polygone;
    for (size_t i : ic) p.sommets.push_back({contour[i].first, contour[i].second});
    if (p.sommets.size() >= 3) {
        double err_p = 0;
        for (const auto& q : contour) {
            double d = 1e18;
            for (size_t i = 0; i < p.sommets.size(); ++i) {
                const Point& a = p.sommets[i];
                const Point& b = p.sommets[(i + 1) % p.sommets.size()];
                d = std::min(d, distance_segment({q.first, q.second}, a, b));
            }
            err_p += d;
        }
        p.erreur = err_p / (double)n;
    } else {
        p.erreur = 1e9;
    }

    if (std::getenv("VISIO_DEBUG_FORME")) {
        std::fprintf(stderr, "FORME modele : ellipse err %.2f px (c %.0f,%.0f a %.0f b %.0f) ; "
                             "polygone %zu sommets err %.2f px\n",
                     e.erreur, e.cx, e.cy, e.a, e.b, p.sommets.size(), p.erreur);
        for (const auto& s : p.sommets) std::fprintf(stderr, "   sommet %.1f,%.1f\n", s.x, s.y);
    }

    if (p.sommets.size() >= 3 && p.erreur <= e.erreur * 1.2) return p;
    return e;
}

namespace {

// La case est libre si elle ne porte ni lettre ni chiffre : la forme a le droit d'ecraser un
// trait d'encre, un point, une bordure. Avant, un trait fin « - » ou « | » de la couche d'encre
// bloquait le caractere du contour, et la pointe du triangle perdait un de ses deux cotes.
bool libre(char c) {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
}

void poser(std::vector<std::string>& gr, int cols, int rows, int x, int y, char c) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) return;
    if (libre(gr[(size_t)y][(size_t)x])) gr[(size_t)y][(size_t)x] = c;
}

// Ecrire un caractere de contour en acceptant de decaler d'UNE case quand la case visee porte
// deja un trait : a la pointe d'un triangle, les deux aretes tombent sur la meme case et la
// seconde etait perdue, ce qui ouvrait la pointe.
void poser_arete(std::vector<std::string>& gr, int cols, int rows, int x, int y, char c) {
    if (x < 0 || y < 0 || x >= cols || y >= rows) return;
    if (libre(gr[(size_t)y][(size_t)x])) {
        gr[(size_t)y][(size_t)x] = c;
        return;
    }
    const int dx = (c == '\\') ? 1 : (c == '/' ? -1 : 0);
    if (!dx) return;
    const int x2 = x + dx;
    if (x2 < 0 || x2 >= cols) return;
    if (libre(gr[(size_t)y][(size_t)x2])) gr[(size_t)y][(size_t)x2] = c;
}

// L'echelle des HAUTEURS dans la case, apprise dans les tutoriels : un trait qui passe en haut
// de la case s'ecrit « " », au milieu « - », en bas « _ » ; les extremites d'une rangee se
// terminent par « . » « , » « ' » « ` ». C'est ce qui arrondit un chapeau au lieu de le faire
// plat comme une barre.
void poser_chapeau(std::vector<std::string>& gr, int cols, int rows, int cy, int cg, int cd,
                   bool haut) {
    const int largeur = cd - cg + 1;
    if (largeur < 3) {
        for (int x = cg; x <= cd; ++x) poser(gr, cols, rows, x, cy, '_');
        return;
    }
    if (largeur < 6) {
        poser(gr, cols, rows, cg, cy, haut ? '.' : '`');
        for (int x = cg + 1; x <= cd - 1; ++x) poser(gr, cols, rows, x, cy, '_');
        poser(gr, cols, rows, cd, cy, haut ? '.' : '\'');
        return;
    }
    // « .-"""-. » en haut, « `-...-' » en bas : la forme canonique du chapeau.
    poser(gr, cols, rows, cg, cy, haut ? '.' : '`');
    poser(gr, cols, rows, cg + 1, cy, '-');
    for (int x = cg + 2; x <= cd - 2; ++x) poser(gr, cols, rows, x, cy, haut ? '"' : '.');
    poser(gr, cols, rows, cd - 1, cy, '-');
    poser(gr, cols, rows, cd, cy, haut ? '.' : '\'');
}

void dessiner_ellipse(std::vector<std::string>& gr, const Forme& f, Rect fen, int cellx, int celly,
                      int cols, int rows) {
    const double yhaut = f.cy - f.b, ybas = f.cy + f.b;
    const int r0 = std::max(0, (int)std::floor((yhaut - fen.y) / celly));
    const int r1 = std::min(rows - 1, (int)std::floor((ybas - fen.y) / celly));
    if (r1 <= r0) return;

    // Rangees REELLEMENT dessinees. On ne saute plus celle dont le centre tombe juste hors de
    // l'ellipse : c'etait la rangee du chapeau, et le rond sortait sans couvercle.
    std::vector<int> dessinees;
    for (int r = r0; r <= r1; ++r) {
        const double y = fen.y + (r + 0.5) * celly;
        if (y > yhaut - celly * 0.5 && y < ybas + celly * 0.5) dessinees.push_back(r);
    }
    if (dessinees.size() < 3) return;

    auto demi = [&](int r) {
        const double y = fen.y + (r + 0.5) * celly;
        const double u = (y - f.cy) / std::max(1e-9, f.b);
        return f.a * std::sqrt(std::max(0.0, 1.0 - std::min(1.0, u * u)));
    };
    double prec_g = 0, prec_d = 0;
    bool premier = true;
    for (size_t k = 0; k < dessinees.size(); ++k) {
        const int r = dessinees[k];
        const double dx = demi(r);
        const double xg = (f.cx - dx - fen.x) / cellx;
        const double xd = (f.cx + dx - fen.x) / cellx;
        int cg = (int)std::floor(xg), cd = (int)std::floor(xd);
        if (k == 0 || k + 1 == dessinees.size()) {
            // Le chapeau s'etend jusqu'a la rangee voisine : a cette echelle le rond perd deux
            // cases par rangee en s'arrondissant, et un chapeau a sa seule largeur flotte.
            const int voisin = (k == 0) ? dessinees[1] : dessinees[k - 1];
            const double dv = demi(voisin);
            cg = std::min(cg, (int)std::floor((f.cx - dv - fen.x) / cellx));
            cd = std::max(cd, (int)std::floor((f.cx + dv - fen.x) / cellx));
            poser_chapeau(gr, cols, rows, r, cg, cd, k == 0);
        } else {
            const double dg = premier ? -1.0 : (std::abs(xg - prec_g) < 0.45 ? 0.0 : (xg > prec_g ? 1.0 : -1.0));
            const double dd = premier ? 1.0 : (std::abs(xd - prec_d) < 0.45 ? 0.0 : (xd > prec_d ? 1.0 : -1.0));
            poser(gr, cols, rows, cg, r, dg > 0 ? '\\' : (dg < 0 ? '/' : '|'));
            if (cd != cg) poser(gr, cols, rows, cd, r, dd > 0 ? '\\' : (dd < 0 ? '/' : '|'));
        }
        prec_g = xg; prec_d = xd; premier = false;
    }
}

void dessiner_polygone(std::vector<std::string>& gr, const Forme& f, Rect fen, int cellx, int celly,
                       int cols, int rows) {
    const size_t n = f.sommets.size();
    if (n < 3) return;
    double ymin = 1e18, ymax = -1e18;
    for (const auto& s : f.sommets) { ymin = std::min(ymin, s.y); ymax = std::max(ymax, s.y); }

    // Pour chaque rangee : le bord gauche et le bord droit donnes par le MODELE (intersection des
    // aretes avec la ligne). Prendre les extremes du modele, et non l'arete courante, rend le
    // trace SYMETRIQUE par construction : avant, les deux cotes d'un triangle pourtant symetrique
    // tombaient sur des rangees differentes et le dessin partait en biais.
    struct Rangee { int r; double xg, xd; };
    std::vector<Rangee> rr;
    const int r0 = (int)std::floor((ymin - fen.y) / celly);
    const int r1 = (int)std::floor((ymax - fen.y) / celly);
    for (int r = r0; r <= r1; ++r) {
        const double y = fen.y + (r + 0.5) * celly;
        double xg = 1e18, xd = -1e18;
        int touches = 0;
        for (size_t i = 0; i < n; ++i) {
            const Point& a = f.sommets[i];
            const Point& b = f.sommets[(i + 1) % n];
            if (std::abs(b.y - a.y) < 1e-9) {
                // arete horizontale : elle occupe la rangee si la ligne la traverse
                if (y >= std::min(a.y, b.y) - celly * 0.5 && y <= std::max(a.y, b.y) + celly * 0.5) {
                    xg = std::min(xg, std::min(a.x, b.x));
                    xd = std::max(xd, std::max(a.x, b.x));
                    ++touches;
                }
                continue;
            }
            const double t = (y - a.y) / (b.y - a.y);
            if (t < -0.02 || t > 1.02) continue;
            const double x = a.x + t * (b.x - a.x);
            xg = std::min(xg, x);
            xd = std::max(xd, x);
            ++touches;
        }
        if (!touches || xg > xd) continue;
        rr.push_back({r, (xg - fen.x) / cellx, (xd - fen.x) / cellx});
    }
    if (rr.size() < 2) return;

    int cg_prec = -1000, cd_prec = -1000;
    double xg_prec = 0, xd_prec = 0;
    for (size_t k = 0; k < rr.size(); ++k) {
        const int r = rr[k].r;
        int cg = (int)std::floor(rr[k].xg), cd = (int)std::floor(rr[k].xd);
        // Un cote ne bouge que d'UNE case par rangee (« every line must connect up »). Sans ce
        // garde-fou, une pente de 1,3 case par rangee saute deux cases par moments et la ligne
        // se coupe ; en remplissant le trou, on obtenait des cotes DOUBLES (« // », « \\ »).
        // On borne donc la position, et le cote reste net.
        if (cg_prec > -999) {
            cg = std::max(cg_prec - 1, std::min(cg_prec + 1, cg));
            cd = std::max(cd_prec - 1, std::min(cd_prec + 1, cd));
        }
        const bool base = (k + 1 == rr.size());
        if (base) {
            // La base : une barre d'un cote a l'autre, coins « , » et « . » poses D'ABORD (les
            // poser apres les « _ » ne marchait pas : la case etait deja prise).
            poser(gr, cols, rows, cg, r, ',');
            poser(gr, cols, rows, cd, r, '.');
            for (int x = cg + 1; x <= cd - 1; ++x) poser(gr, cols, rows, x, r, '_');
            break;
        }
        // Le CARACTERE suit le deplacement REEL du bord, en fraction de case (seuil 0,35), comme
        // pour l'ellipse : un bord qui glisse de 0,6 case n'a pas change de case mais il PENDU,
        // donc « \ » et non « | ». Premiere rangee : la pente se lit sur la suivante.
        const bool premiere = (cg_prec < -999);
        const double dgf = premiere ? (rr[1].xg - rr[0].xg) : (rr[k].xg - xg_prec);
        const double ddf = premiere ? (rr[1].xd - rr[0].xd) : (rr[k].xd - xd_prec);
        const int dg = std::abs(dgf) < 0.35 ? 0 : (dgf > 0 ? 1 : -1);
        const int dd = std::abs(ddf) < 0.35 ? 0 : (ddf > 0 ? 1 : -1);
        if (cg == cd) {
            if (std::getenv("VISIO_DEBUG_FORME"))
                std::fprintf(stderr, "POINTE r %d cg %d cd %d cases '%c''%c'\n", r, cg, cd,
                             gr[(size_t)r][(size_t)cg], gr[(size_t)r][(size_t)cg + 1]);
            poser_arete(gr, cols, rows, cg, r, '/');
            poser_arete(gr, cols, rows, cg + 1, r, '\\');
            cd = cg + 1;
        } else {
            const char car_g = dg > 0 ? '\\' : (dg < 0 ? '/' : '|');
            const char car_d = dd > 0 ? '\\' : (dd < 0 ? '/' : '|');
            poser_arete(gr, cols, rows, cg, r, car_g);
            poser_arete(gr, cols, rows, cd, r, car_d);
        }
        cg_prec = cg; cd_prec = cd;
        xg_prec = rr[k].xg; xd_prec = rr[k].xd;
    }
}

} // namespace

void dessiner_forme(std::vector<std::string>& gr, const Forme& f, Rect fen, int cellx, int celly,
                    int cols, int rows) {
    if (std::getenv("VISIO_DEBUG_FORME"))
        std::fprintf(stderr, "FORME dessin fen %d,%d %dx%d  cell %dx%d\n", fen.x, fen.y, fen.w, fen.h,
                     cellx, celly);
    if (f.genre == Forme::Genre::Ellipse) dessiner_ellipse(gr, f, fen, cellx, celly, cols, rows);
    else if (f.genre == Forme::Genre::Polygone) dessiner_polygone(gr, f, fen, cellx, celly, cols, rows);
}

} // namespace visio
