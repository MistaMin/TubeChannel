#pragma once

// Loads a small SPICE-style netlist into a Circuit.
//
// Supported: R, C, L, D, Q, and X instances of the built-in sub-circuits OPAMP, TRIODE, MPSA29,
// TIP112; .model (D, NPN, PNP); .param; values and parameters with SI suffixes; values written as
// {expression} become run-time variable elements (pots, switches) re-evaluated when a parameter
// changes. An optional trailing `tol=N` on R/C lines gives the part tolerance in percent.

#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Circuit.h"

namespace tubechannel::circuit
{
inline double parseNumber(const std::string& textIn)
{
    std::string t;
    for (char ch : textIn)
        t.push_back(char(std::tolower(static_cast<unsigned char>(ch))));
    size_t pos = 0;
    double v = std::stod(t, &pos);
    std::string suf = t.substr(pos);
    double mult = 1.0;
    if (suf.rfind("meg", 0) == 0) mult = 1e6;
    else if (!suf.empty())
    {
        switch (suf[0])
        {
            case 't': mult = 1e12; break;
            case 'g': mult = 1e9; break;
            case 'k': mult = 1e3; break;
            case 'm': mult = 1e-3; break;
            case 'u': mult = 1e-6; break;
            case 'n': mult = 1e-9; break;
            case 'p': mult = 1e-12; break;
            case 'f': mult = 1e-15; break;
            default: break;
        }
    }
    return v * mult;
}

// ---- expressions ------------------------------------------------------------------------------------
struct Expr
{
    enum Kind { Num, Var, Neg, Add, Sub, Mul, Div, Gt, Lt, If, Min, Max, Pow };
    Kind kind = Num;
    double num = 0.0;
    std::string name;
    std::vector<std::unique_ptr<Expr>> kids;

    double eval(const std::map<std::string, double>& params) const
    {
        switch (kind)
        {
            case Num: return num;
            case Var:
            {
                auto it = params.find(name);
                if (it == params.end())
                    throw std::runtime_error("unknown parameter " + name);
                return it->second;
            }
            case Neg: return -kids[0]->eval(params);
            case Add: return kids[0]->eval(params) + kids[1]->eval(params);
            case Sub: return kids[0]->eval(params) - kids[1]->eval(params);
            case Mul: return kids[0]->eval(params) * kids[1]->eval(params);
            case Div: return kids[0]->eval(params) / kids[1]->eval(params);
            case Gt: return kids[0]->eval(params) > kids[1]->eval(params) ? 1.0 : 0.0;
            case Lt: return kids[0]->eval(params) < kids[1]->eval(params) ? 1.0 : 0.0;
            case If: return kids[0]->eval(params) != 0.0 ? kids[1]->eval(params) : kids[2]->eval(params);
            case Min: return std::min(kids[0]->eval(params), kids[1]->eval(params));
            case Max: return std::max(kids[0]->eval(params), kids[1]->eval(params));
            case Pow: return std::pow(kids[0]->eval(params), kids[1]->eval(params));
        }
        return 0.0;
    }
};

class ExprParser
{
public:
    static std::unique_ptr<Expr> parse(const std::string& s)
    {
        ExprParser p(s);
        auto e = p.cond();
        p.skip();
        if (p.i != s.size())
            throw std::runtime_error("bad expression: " + s);
        return e;
    }

private:
    explicit ExprParser(const std::string& s_) : s(s_) {}
    const std::string& s;
    size_t i = 0;

    void skip() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
    static std::unique_ptr<Expr> make(Expr::Kind k) { auto e = std::make_unique<Expr>(); e->kind = k; return e; }
    static std::unique_ptr<Expr> bin(Expr::Kind k, std::unique_ptr<Expr> a, std::unique_ptr<Expr> b)
    {
        auto e = make(k);
        e->kids.push_back(std::move(a));
        e->kids.push_back(std::move(b));
        return e;
    }

    std::unique_ptr<Expr> cond()
    {
        auto a = sum();
        skip();
        if (i < s.size() && (s[i] == '>' || s[i] == '<'))
        {
            const bool gt = s[i++] == '>';
            auto b = sum();
            return bin(gt ? Expr::Gt : Expr::Lt, std::move(a), std::move(b));
        }
        return a;
    }
    std::unique_ptr<Expr> sum()
    {
        auto a = prod();
        for (;;)
        {
            skip();
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
            {
                const bool plus = s[i++] == '+';
                a = bin(plus ? Expr::Add : Expr::Sub, std::move(a), prod());
            }
            else
                return a;
        }
    }
    std::unique_ptr<Expr> prod()
    {
        auto a = unary();
        for (;;)
        {
            skip();
            if (i < s.size() && (s[i] == '*' || s[i] == '/'))
            {
                const bool mul = s[i++] == '*';
                a = bin(mul ? Expr::Mul : Expr::Div, std::move(a), unary());
            }
            else
                return a;
        }
    }
    std::unique_ptr<Expr> unary()
    {
        skip();
        if (i < s.size() && s[i] == '-')
        {
            ++i;
            auto e = make(Expr::Neg);
            e->kids.push_back(unary());
            return e;
        }
        return primary();
    }
    std::unique_ptr<Expr> primary()
    {
        skip();
        if (i < s.size() && s[i] == '(')
        {
            ++i;
            auto e = cond();
            skip();
            ++i; // ')'
            return e;
        }
        if (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.'))
        {
            size_t j = i;
            while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.'))
            {
                // allow exponent signs like 1e-3
                if ((s[j] == 'e' || s[j] == 'E') && j + 1 < s.size() && (s[j + 1] == '-' || s[j + 1] == '+') &&
                    j + 2 < s.size() && std::isdigit(static_cast<unsigned char>(s[j + 2])))
                    j += 2;
                ++j;
            }
            auto e = make(Expr::Num);
            e->num = parseNumber(s.substr(i, j - i));
            i = j;
            return e;
        }
        size_t j = i;
        while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
            ++j;
        std::string id = s.substr(i, j - i);
        i = j;
        skip();
        if (i < s.size() && s[i] == '(')
        {
            ++i;
            std::vector<std::unique_ptr<Expr>> args;
            for (;;)
            {
                args.push_back(cond());
                skip();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                break;
            }
            ++i; // ')'
            Expr::Kind k = Expr::If;
            if (id == "if" || id == "ternary_fcn") k = Expr::If;
            else if (id == "min") k = Expr::Min;
            else if (id == "max") k = Expr::Max;
            else if (id == "pwr" || id == "pow") k = Expr::Pow;
            else throw std::runtime_error("unknown function " + id);
            auto e = make(k);
            e->kids = std::move(args);
            return e;
        }
        auto e = make(Expr::Var);
        e->name = id;
        return e;
    }
};

// ---- loader -----------------------------------------------------------------------------------------
struct ToleranceSettings
{
    bool enabled = false;
    unsigned seed = 1;
};

class Netlist
{
public:
    struct VarElement
    {
        std::string ref;
        int handle = -1;                              // resistor handle in the Circuit
        std::unique_ptr<Expr> expr;
        double tolScale = 1.0;
    };

    std::map<std::string, double> params;
    std::map<std::string, int> opamps;                // by instance name, e.g. "A1A"
    std::map<std::string, int> resistors;             // by designator
    std::vector<VarElement> vars;
    std::vector<std::string> warnings;

    // Parse and instantiate into `c`. Node names are shared with other loads on the same circuit.
    void load(Circuit& c, const std::string& text, const ToleranceSettings& tol = {})
    {
        std::mt19937 rng(tol.seed);
        std::normal_distribution<double> gauss(0.0, 1.0);
        auto tolFactor = [&](double pct) -> double
        {
            if (!tol.enabled || pct <= 0.0)
                return 1.0;
            double g = gauss(rng) * (pct / 2.0);
            g = std::max(-pct, std::min(pct, g));
            return 1.0 + g / 100.0;
        };

        std::istringstream in(text);
        std::string raw;
        std::vector<std::vector<std::string>> lines;
        while (std::getline(in, raw))
        {
            const auto sc = raw.find(';');
            if (sc != std::string::npos)
                raw = raw.substr(0, sc);
            std::vector<std::string> tok = tokenize(raw);
            if (tok.empty() || tok[0][0] == '*')
                continue;
            lines.push_back(std::move(tok));
        }

        // pass 1: params and models
        for (auto& t : lines)
        {
            const std::string d = lower(t[0]);
            if (d == ".param")
            {
                for (size_t k = 1; k < t.size(); ++k)
                {
                    const auto eq = t[k].find('=');
                    if (eq != std::string::npos)
                        params[t[k].substr(0, eq)] = value(t[k].substr(eq + 1));
                }
            }
            else if (d == ".model")
                parseModel(t);
        }

        // pass 2: elements
        for (auto& t : lines)
        {
            const std::string name = t[0];
            const char kind = char(std::toupper(static_cast<unsigned char>(name[0])));
            if (name[0] == '.')
                continue;
            if (kind == 'R' && t.size() >= 4)
            {
                const double pct = tolOf(t, 4);
                const int a = c.node(t[1]), b = c.node(t[2]);
                if (isExpr(t[3]))
                {
                    VarElement v;
                    v.ref = name;
                    v.expr = ExprParser::parse(strip(t[3]));
                    v.tolScale = tolFactor(pct);
                    v.handle = c.addResistor(a, b, std::max(1e-6, v.expr->eval(params) * v.tolScale));
                    resistors[name] = v.handle;
                    vars.push_back(std::move(v));
                }
                else
                    resistors[name] = c.addResistor(a, b, value(t[3]) * tolFactor(pct));
            }
            else if (kind == 'C' && t.size() >= 4)
                c.addCapacitor(c.node(t[1]), c.node(t[2]), value(t[3]) * tolFactor(tolOf(t, 4)));
            else if (kind == 'L' && t.size() >= 4)
                c.addInductor(c.node(t[1]), c.node(t[2]), value(t[3]));
            else if (kind == 'D' && t.size() >= 4)
                c.addDiode(c.node(t[1]), c.node(t[2]), diodeModels.at(t[3]));
            else if (kind == 'Q' && t.size() >= 5)
                c.addBjt(c.node(t[1]), c.node(t[2]), c.node(t[3]), bjtModels.at(t[4]));
            else if (kind == 'X')
                instance(c, t);
            else if (kind == 'B' || kind == 'V' || kind == 'E' || kind == 'I')
                warnings.push_back("skipped " + name);
            else
                throw std::runtime_error("unsupported element " + name);
        }
    }

    // Re-evaluate every variable element after parameters change. Returns whether anything moved.
    void update(Circuit& c)
    {
        for (auto& v : vars)
            c.setResistance(v.handle, std::max(1e-6, v.expr->eval(params) * v.tolScale));
    }

private:
    std::map<std::string, DiodeModel> diodeModels;
    std::map<std::string, BjtModel> bjtModels;

    static std::string lower(std::string s)
    {
        for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    }

    static bool isExpr(const std::string& s) { return !s.empty() && s[0] == '{'; }
    static std::string strip(const std::string& s)
    {
        return (s.size() >= 2 && s.front() == '{' && s.back() == '}') ? s.substr(1, s.size() - 2) : s;
    }

    double value(const std::string& s) const
    {
        if (isExpr(s))
        {
            auto e = ExprParser::parse(strip(s));
            return e->eval(params);
        }
        auto it = params.find(s);
        if (it != params.end())
            return it->second;
        return parseNumber(s);
    }

    static double tolOf(const std::vector<std::string>& t, size_t from)
    {
        for (size_t k = from; k < t.size(); ++k)
            if (lower(t[k]).rfind("tol=", 0) == 0)
                return std::stod(t[k].substr(4));
        return 0.0;
    }

    // Split on whitespace, but keep {...} groups (which may contain spaces) as one token.
    static std::vector<std::string> tokenize(const std::string& line)
    {
        std::vector<std::string> out;
        std::string cur;
        int depth = 0;
        for (char ch : line)
        {
            if (ch == '{') ++depth;
            if (ch == '}') --depth;
            if (depth == 0 && (std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')'))
            {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            }
            else
                cur.push_back(ch);
        }
        if (!cur.empty())
            out.push_back(cur);
        return out;
    }

    void parseModel(const std::vector<std::string>& t)
    {
        if (t.size() < 3)
            return;
        const std::string type = lower(t[2]);
        std::map<std::string, double> kv;
        for (size_t k = 3; k < t.size(); ++k)
        {
            const auto eq = t[k].find('=');
            if (eq != std::string::npos)
                kv[lower(t[k].substr(0, eq))] = value(t[k].substr(eq + 1));
        }
        auto get = [&](const char* key, double def) { auto it = kv.find(key); return it == kv.end() ? def : it->second; };
        if (type == "d")
        {
            DiodeModel m;
            m.IS = get("is", 1e-14); m.N = get("n", 1.0); m.RS = get("rs", 0.0);
            m.BV = get("bv", 0.0); m.IBV = get("ibv", 1e-3);
            if (m.RS < 2.0) m.RS = 0.0;       // below 2 ohm it is lost next to the real source resistances
            diodeModels[t[1]] = m;
        }
        else if (type == "npn" || type == "pnp")
        {
            BjtModel m;
            m.pnp = type == "pnp";
            m.IS = get("is", 1e-14); m.BF = get("bf", 100.0); m.BR = get("br", 1.0);
            m.VAF = get("vaf", 0.0); m.RB = get("rb", 0.0); m.CJC = get("cjc", 0.0); m.CJE = get("cje", 0.0);
            // Base resistance only matters for the low-noise input pair; the junction capacitances sit
            // above the audio band by orders of magnitude. Dropping both removes nodes and states.
            if (m.RB <= 25.0) m.RB = 0.0;
            m.CJC = m.CJE = 0.0;
            bjtModels[t[1]] = m;
        }
    }

    void instance(Circuit& c, const std::vector<std::string>& t)
    {
        // Xname n1 n2 ... SUBCKT params: k=v ...
        size_t sub = 0;
        for (size_t k = 1; k < t.size(); ++k)
        {
            const std::string up = t[k];
            if (up == "OPAMP" || up == "TRIODE" || up == "MPSA29" || up == "TIP112")
            {
                sub = k;
                break;
            }
        }
        if (sub == 0)
            throw std::runtime_error("unknown sub-circuit in " + t[0]);
        std::map<std::string, double> kv;
        for (size_t k = sub + 1; k < t.size(); ++k)
        {
            const auto eq = t[k].find('=');
            if (eq != std::string::npos)
                kv[lower(t[k].substr(0, eq))] = value(t[k].substr(eq + 1));
        }
        auto get = [&](const char* key, double def) { auto it = kv.find(key); return it == kv.end() ? def : it->second; };
        const std::string& kind = t[sub];
        const std::string inst = t[0].substr(1);
        if (kind == "OPAMP")
        {
            OpAmpModel m;
            m.GBW = get("gbw", 10e6);
            m.SR = get("sr", 9.0) * 1e6;
            m.AOL = get("aol", 1e5);
            m.VSW = get("vsw", 13.0);
            m.ROUT = get("rout", 10.0);
            opamps[inst] = c.addOpAmp(c.node(t[1]), c.node(t[2]), c.node(t[3]), m);
        }
        else if (kind == "TRIODE")
        {
            TriodeModel m;
            m.MU = get("mu", 105); m.EX = get("ex", 1.1); m.KG1 = get("kg1", 825);
            m.KP = get("kp", 350); m.KVB = get("kvb", 3000);
            m.CGK = get("cgk", 1.6e-12); m.CGP = get("cgp", 1.7e-12); m.CPK = get("cpk", 0.46e-12);
            c.addTriode(c.node(t[1]), c.node(t[2]), c.node(t[3]), m);
        }
        else if (kind == "MPSA29")
        {
            BjtModel m;
            m.IS = 1e-14; m.BF = 100; m.VAF = 100;
            const int mid = c.node(inst + ".mid");
            c.addBjt(c.node(t[1]), c.node(t[2]), mid, m);
            c.addBjt(c.node(t[1]), mid, c.node(t[3]), m);
            c.addResistor(mid, c.node(t[3]), 10e3);
        }
        else if (kind == "TIP112")
        {
            BjtModel m;
            m.IS = 1e-13; m.BF = 100; m.VAF = 100;
            const int mid = c.node(inst + ".mid");
            c.addBjt(c.node(t[1]), c.node(t[2]), mid, m);
            c.addBjt(c.node(t[1]), mid, c.node(t[3]), m);
            c.addResistor(mid, c.node(t[3]), 150.0);
            c.addResistor(c.node(t[2]), c.node(t[3]), 8e3);
        }
    }
};
} // namespace tubechannel::circuit
