// src/perceive/ocr.cpp — le texte d'une zone, lu par tesseract (binaire, CPU).
#include "perceive/ocr.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace visio {
namespace {

// Ecrit une zone de la frame en PPM binaire (P6, 3 octets par pixel). Tesseract ne lit pas
// autre chose sans bibliotheque, et le PPM evite toute dependance a un encodeur PNG.
std::string ecrire_ppm(const Frame& f, const Rect& r, bool inverser) {
    std::string chemin = "/tmp/visio_ocr_" + std::to_string((long)getpid()) + ".ppm";
    FILE* o = std::fopen(chemin.c_str(), "wb");
    if (!o) return {};
    std::fprintf(o, "P6\n%d %d\n255\n", r.w, r.h);
    std::vector<unsigned char> ligne((size_t)r.w * 3);
    for (int y = 0; y < r.h; ++y) {
        const uint32_t* src = f.ligne(r.y + y) + (size_t)(r.x - f.region.x);
        for (int x = 0; x < r.w; ++x) {
            const uint32_t p = src[x];
            unsigned char b = (unsigned char)(p & 0xFF);
            unsigned char g = (unsigned char)((p >> 8) & 0xFF);
            unsigned char rr = (unsigned char)((p >> 16) & 0xFF);
            if (inverser) { b = (unsigned char)(255 - b); g = (unsigned char)(255 - g); rr = (unsigned char)(255 - rr); }
            ligne[(size_t)x * 3 + 0] = rr;
            ligne[(size_t)x * 3 + 1] = g;
            ligne[(size_t)x * 3 + 2] = b;
        }
        std::fwrite(ligne.data(), 1, ligne.size(), o);
    }
    std::fclose(o);
    return chemin;
}

// Tesseract sort un TSV : niveau, page, bloc, paragraphe, ligne, mot, x, y, l, h, conf, texte.
// On regroupe par (bloc, paragraphe, ligne) et on garde le y du premier mot.
std::vector<LigneOcr> lire_tsv(const char* tsv, int hmax) {
    std::vector<LigneOcr> out;
    std::vector<std::pair<double, int>> confs;   // somme des confiances, nombre de mots
    const char* p = tsv;
    int cle_courante[3] = {-1, -1, -1};
    while (*p) {
        const char* fin = std::strchr(p, '\n');
        if (!fin) break;
        std::string ligne(p, (size_t)(fin - p));
        p = fin + 1;
        // decoupage en champs tabules
        std::vector<std::string> c;
        size_t deb = 0;
        while (true) {
            const size_t t = ligne.find('\t', deb);
            if (t == std::string::npos) { c.push_back(ligne.substr(deb)); break; }
            c.push_back(ligne.substr(deb, t - deb));
            deb = t + 1;
        }
        if (c.size() < 12) continue;
        if (c[0] != "5") continue;                       // niveau mot
        if (c[11].empty()) continue;
        const int bloc = std::atoi(c[2].c_str()), par = std::atoi(c[3].c_str()),
                  lin = std::atoi(c[4].c_str());
        const int y = std::atoi(c[7].c_str()), h = std::atoi(c[9].c_str());
        const std::string mot = c[11];
        if (!std::strcmp(mot.c_str(), "|") || mot == "." || mot == "-") continue;
        const double conf = std::atof(c[10].c_str());
        if (cle_courante[0] != bloc || cle_courante[1] != par || cle_courante[2] != lin) {
            cle_courante[0] = bloc; cle_courante[1] = par; cle_courante[2] = lin;
            out.push_back({y, h, 0.0, mot});
            confs.push_back({conf, 1});
        } else {
            out.back().texte += " ";
            out.back().texte += mot;
            confs.back().first += conf;
            confs.back().second += 1;
        }
        if ((int)out.size() > hmax) break;
    }
    for (size_t i = 0; i < out.size() && i < confs.size(); ++i)
        out[i].conf = confs[i].second ? confs[i].first / confs[i].second : 0.0;
    return out;
}

} // namespace

bool ocr_disponible() {
    static int cache = -1;
    if (cache < 0) cache = (std::system("tesseract --version >/dev/null 2>&1") == 0) ? 1 : 0;
    return cache == 1;
}

namespace {

// Garde les lignes qui portent vraiment du texte : deux lettres au moins et la confiance du
// moteur. Une forme (un rond, un triangle) se lit en bouillie (« PO / NE ») et passe sinon.
std::vector<LigneOcr> filtrer(std::vector<LigneOcr> lignes) {
    std::vector<LigneOcr> garde;
    for (const auto& l : lignes) {
        size_t lettres = 0;
        for (char ch : l.texte)
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
                ++lettres;
        if (lettres >= 2 && l.conf >= 55.0) garde.push_back(l);
    }
    return garde;
}

std::string lancer_tesseract(const std::string& ppm, const char* langue) {
    const std::string cmd = "tesseract " + ppm + " stdout -l " + langue + " --psm 6 tsv 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {};
    std::string sortie;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) sortie.append(buf, n);
    pclose(p);
    return sortie;
}

double luma_zone(const Frame& f, const Rect& r) {
    double luma = 0;
    for (int y = 0; y < r.h; y += 2) {
        const uint32_t* l = f.ligne(r.y + y) + (size_t)(r.x - f.region.x);
        for (int x = 0; x < r.w; x += 2) {
            const uint32_t p = l[x];
            luma += 0.114 * (double)(p & 0xFF) + 0.587 * (double)((p >> 8) & 0xFF) +
                    0.299 * (double)((p >> 16) & 0xFF);
        }
    }
    return luma / (double)(((r.w + 1) / 2) * ((r.h + 1) / 2));
}

} // namespace

std::vector<LigneOcr> ocr_zone(const Frame& f, Rect r, const char* langue) {
    std::vector<LigneOcr> vide;
    r = intersect(r, f.region);
    // Une ligne de texte fait au moins 8 px de haut ; en dessous il n'y a rien a lire.
    if (r.empty() || r.w < 12 || r.h < 8) return vide;

    // Fond sombre : tesseract attend du texte noir sur fond blanc. On inverse quand la zone
    // est majoritairement sombre (un terminal, un theme sombre).
    std::string ppm = ecrire_ppm(f, r, luma_zone(f, r) < 110.0);
    if (ppm.empty()) return vide;
    const std::string sortie = lancer_tesseract(ppm, langue);
    std::remove(ppm.c_str());
    if (sortie.empty()) return vide;
    std::vector<LigneOcr> lignes = lire_tsv(sortie.c_str(), 40);
    return filtrer(std::move(lignes));
}

std::vector<std::vector<LigneOcr>> ocr_zones(const Frame& f, const std::vector<Rect>& zones,
                                             const char* langue) {
    std::vector<std::vector<LigneOcr>> out(zones.size());
    if (zones.empty()) return out;
    // 1. Empiler les zones dans une image, une bande blanche de 12 px entre elles.
    const int BANDE = 12;
    int largeur = 0, hauteur = 0;
    std::vector<int> decalage(zones.size(), 0);
    for (size_t i = 0; i < zones.size(); ++i) {
        const Rect r = intersect(zones[i], f.region);
        if (r.empty() || r.w < 12 || r.h < 8) continue;
        decalage[i] = hauteur;
        hauteur += r.h + BANDE;
        largeur = std::max(largeur, r.w);
    }
    if (!hauteur || !largeur) return out;

    const std::string ppm = "/tmp/visio_ocr_" + std::to_string((long)getpid()) + "_lot.ppm";
    FILE* o = std::fopen(ppm.c_str(), "wb");
    if (!o) return out;
    std::fprintf(o, "P6\n%d %d\n255\n", largeur, hauteur);
    std::vector<unsigned char> blanc((size_t)largeur * 3, 255);
    std::vector<unsigned char> ligne((size_t)largeur * 3, 255);
    for (size_t i = 0; i < zones.size(); ++i) {
        const Rect r = intersect(zones[i], f.region);
        if (r.empty() || r.w < 12 || r.h < 8) continue;
        const bool inverser = luma_zone(f, r) < 110.0;
        for (int y = 0; y < r.h; ++y) {
            std::fill(ligne.begin(), ligne.end(), 255);
            const uint32_t* src = f.ligne(r.y + y) + (size_t)(r.x - f.region.x);
            for (int x = 0; x < r.w; ++x) {
                const uint32_t p = src[x];
                unsigned char b = (unsigned char)(p & 0xFF);
                unsigned char g = (unsigned char)((p >> 8) & 0xFF);
                unsigned char rr = (unsigned char)((p >> 16) & 0xFF);
                if (inverser) { b = (unsigned char)(255 - b); g = (unsigned char)(255 - g); rr = (unsigned char)(255 - rr); }
                ligne[(size_t)x * 3 + 0] = rr;
                ligne[(size_t)x * 3 + 1] = g;
                ligne[(size_t)x * 3 + 2] = b;
            }
            std::fwrite(ligne.data(), 1, ligne.size(), o);
        }
        for (int k = 0; k < BANDE; ++k) std::fwrite(blanc.data(), 1, blanc.size(), o);
    }
    std::fclose(o);

    // 2. Un seul passage du moteur, puis chaque ligne retrouve sa zone par son ordonnee.
    const std::string tsv = lancer_tesseract(ppm, langue);
    std::remove(ppm.c_str());
    if (tsv.empty()) return out;
    for (const auto& l : filtrer(lire_tsv(tsv.c_str(), 400))) {
        for (size_t i = 0; i < zones.size(); ++i) {
            const Rect r = intersect(zones[i], f.region);
            if (r.empty() || r.w < 12 || r.h < 8) continue;
            if (l.y >= decalage[i] && l.y < decalage[i] + r.h + BANDE / 2) {
                LigneOcr copie = l;
                copie.y = l.y - decalage[i];
                out[i].push_back(copie);
                break;
            }
        }
    }
    return out;
}

} // namespace visio
