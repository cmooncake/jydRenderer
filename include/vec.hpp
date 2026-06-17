#pragma once

#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>

template<int n>
struct vec {
    double data[n] = {0};

    vec() = default;

    explicit vec(double v) {
        for (int i = 0; i < n; ++i) {
            data[i] = v;
        }
    }

    double& operator[](int i) {
        assert(i >= 0 && i < n);
        return data[i];
    }

    double operator[](int i) const {
        assert(i >= 0 && i < n);
        return data[i];
    }
};

template<>
struct vec<2>;

template<>
struct vec<3>;

template<>
struct vec<4>;

template<>
struct vec<2> {
    double x = 0;
    double y = 0;

    vec() = default;
    vec(double x_, double y_) : x(x_), y(y_) {}
    explicit vec(double v) : x(v), y(v) {}
    explicit vec(const vec<3>& v);

    double& operator[](int i) {
        assert(i >= 0 && i < 2);
        return i ? y : x;
    }

    double operator[](int i) const {
        assert(i >= 0 && i < 2);
        return i ? y : x;
    }
};

template<>
struct vec<3> {
    double x = 0;
    double y = 0;
    double z = 0;

    vec() = default;
    vec(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    explicit vec(double v) : x(v), y(v), z(v) {}
    explicit vec(const vec<2>& v, double zVal = 0) : x(v.x), y(v.y), z(zVal) {}
    explicit vec(const vec<4>& v);

    double& operator[](int i) {
        assert(i >= 0 && i < 3);
        return i ? (1 == i ? y : z) : x;
    }

    double operator[](int i) const {
        assert(i >= 0 && i < 3);
        return i ? (1 == i ? y : z) : x;
    }
};

template<>
struct vec<4> {
    double x = 0;
    double y = 0;
    double z = 0;
    double w = 0;

    vec() = default;
    vec(double x_, double y_, double z_, double wVal)
        : x(x_), y(y_), z(z_) {
        this->w = wVal;
    }
    explicit vec(double v) : x(v), y(v), z(v) {
        this->w = v;
    }
    explicit vec(const vec<3>& v, double wVal = 1) : x(v.x), y(v.y), z(v.z) {
        this->w = wVal;
    }
    explicit vec(const vec<2>& v, double zVal = 0, double wVal = 1)
        : x(v.x), y(v.y), z(zVal) {
        this->w = wVal;
    }

    double& operator[](int i) {
        assert(i >= 0 && i < 4);
        switch (i) {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: return this->w;
        }
    }

    double operator[](int i) const {
        assert(i >= 0 && i < 4);
        switch (i) {
        case 0: return x;
        case 1: return y;
        case 2: return z;
        default: return this->w;
        }
    }
};

using vec2 = vec<2>;
using vec3 = vec<3>;
using vec4 = vec<4>;

inline vec<2>::vec(const vec<3>& v) : x(v.x), y(v.y) {}

inline vec<3>::vec(const vec<4>& v) : x(v.x), y(v.y), z(v.z) {}

template<int n>
inline vec<n> operator+(vec<n> lhs, const vec<n>& rhs) {
    for (int i = 0; i < n; ++i) {
        lhs[i] += rhs[i];
    }
    return lhs;
}

template<int n>
inline vec<n> operator-(vec<n> lhs, const vec<n>& rhs) {
    for (int i = 0; i < n; ++i) {
        lhs[i] -= rhs[i];
    }
    return lhs;
}

template<int n>
inline vec<n> operator*(vec<n> v, double s) {
    for (int i = 0; i < n; ++i) {
        v[i] *= s;
    }
    return v;
}

template<int n>
inline vec<n> operator*(double s, vec<n> v) {
    return v * s;
}

template<int n>
inline vec<n> operator/(vec<n> v, double s) {
    for (int i = 0; i < n; ++i) {
        v[i] /= s;
    }
    return v;
}

template<int n>
inline vec<n> operator-(const vec<n>& v) {
    return v * -1.0;
}

template<int n>
inline vec<n>& operator+=(vec<n>& lhs, const vec<n>& rhs) {
    lhs = lhs + rhs;
    return lhs;
}

template<int n>
inline vec<n>& operator-=(vec<n>& lhs, const vec<n>& rhs) {
    lhs = lhs - rhs;
    return lhs;
}

template<int n>
inline vec<n>& operator*=(vec<n>& v, double s) {
    v = v * s;
    return v;
}

template<int n>
inline vec<n>& operator/=(vec<n>& v, double s) {
    v = v / s;
    return v;
}

template<int n>
inline double operator*(const vec<n>& a, const vec<n>& b) {
    double ret = 0;
    for (int i = 0; i < n; ++i) {
        ret += a[i] * b[i];
    }
    return ret;
}

template<int n>
inline double norm(const vec<n>& v) {
    return std::sqrt(v * v);
}

template<int n>
inline vec<n> normalize(const vec<n>& v) {
    return v / norm(v);
}

inline vec3 cross(const vec3& a, const vec3& b) {
    return vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

inline vec3 operator^(const vec3& a, const vec3& b) {
    return cross(a, b);
}

template<int n>
inline std::ostream& operator<<(std::ostream& out, const vec<n>& v) {
    for (int i = 0; i < n; ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << v[i];
    }
    return out;
}

template<int n, int m>
struct mat {
    vec<m> rows[n] = {};

    mat() = default;
    mat(const double (&matrix)[n][m])
    {
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < m; ++j) {
                rows[i][j] = matrix[i][j];
            }
        }
    }

    vec<m>& operator[](int i) {
        assert(i >= 0 && i < n);
        return rows[i];
    }

    const vec<m>& operator[](int i) const {
        assert(i >= 0 && i < n);
        return rows[i];
    }
};

using mat2 = mat<2, 2>;
using mat3 = mat<3, 3>;
using mat4 = mat<4, 4>;

template<int n>
inline mat<n, n> identity() {
    mat<n, n> r;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            r[i][j] = (i == j) ? 1.0 : 0.0;
        }
    }
    return r;
}

template<int n, int m>
inline mat<n, m> operator+(mat<n, m> lhs, const mat<n, m>& rhs) {
    for (int i = 0; i < n; ++i) {
        lhs[i] = lhs[i] + rhs[i];
    }
    return lhs;
}

template<int n, int m>
inline mat<n, m> operator-(mat<n, m> lhs, const mat<n, m>& rhs) {
    for (int i = 0; i < n; ++i) {
        lhs[i] = lhs[i] - rhs[i];
    }
    return lhs;
}

template<int n, int m>
inline mat<n, m> operator*(mat<n, m> m_, double s) {
    for (int i = 0; i < n; ++i) {
        m_[i] = m_[i] * s;
    }
    return m_;
}

template<int n, int m>
inline mat<n, m> operator*(double s, mat<n, m> m_) {
    return m_ * s;
}

template<int n, int m>
inline mat<n, m> operator/(mat<n, m> m_, double s) {
    return m_ * (1.0 / s);
}

template<int n, int m>
inline mat<n, m>& operator+=(mat<n, m>& lhs, const mat<n, m>& rhs) {
    lhs = lhs + rhs;
    return lhs;
}

template<int n, int m>
inline mat<n, m>& operator-=(mat<n, m>& lhs, const mat<n, m>& rhs) {
    lhs = lhs - rhs;
    return lhs;
}

template<int n, int m>
inline mat<n, m>& operator*=(mat<n, m>& m_, double s) {
    m_ = m_ * s;
    return m_;
}

template<int n, int m, int k>
inline mat<n, k> operator*(const mat<n, m>& a, const mat<m, k>& b) {
    mat<n, k> r;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < k; ++j) {
            for (int l = 0; l < m; ++l) {
                r[i][j] = r[i][j] + a[i][l] * b[l][j];
            }
        }
    }
    return r;
}

template<int n, int m>
inline vec<n> operator*(const mat<n, m>& a, const vec<m>& v) {
    vec<n> r;
    for (int i = 0; i < n; ++i) {
        r[i] = a[i] * v;
    }
    return r;
}

template<int n, int m>
inline vec<m> operator*(const vec<n>& v, const mat<n, m>& a) {
    vec<m> r;
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < n; ++i) {
            r[j] = r[j] + v[i] * a[i][j];
        }
    }
    return r;
}

template<int n, int m>
inline mat<m, n> transpose(const mat<n, m>& m_) {
    mat<m, n> r;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < m; ++j) {
            r[j][i] = m_[i][j];
        }
    }
    return r;
}

template<int n>
inline double determinant(mat<n, n> a) {
    double det = 1.0;

    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row) {
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) {
                pivot = row;
            }
        }

        if (std::abs(a[pivot][col]) < 1e-12) {
            return 0.0;
        }

        if (pivot != col) {
            std::swap(a.rows[col], a.rows[pivot]);
            det = -det;
        }

        det *= a[col][col];
        const double invPivot = 1.0 / a[col][col];

        for (int j = col + 1; j < n; ++j) {
            a[col][j] *= invPivot;
        }

        for (int row = col + 1; row < n; ++row) {
            const double factor = a[row][col];
            for (int j = col + 1; j < n; ++j) {
                a[row][j] -= factor * a[col][j];
            }
        }
    }

    return det;
}

template<int n>
inline mat<n, n> inverse(const mat<n, n>& m_) {
    mat<n, n> a = m_;
    mat<n, n> inv = identity<n>();

    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row) {
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) {
                pivot = row;
            }
        }

        if (std::abs(a[pivot][col]) < 1e-12) {
            return mat<n, n>{};
        }

        if (pivot != col) {
            std::swap(a.rows[col], a.rows[pivot]);
            std::swap(inv.rows[col], inv.rows[pivot]);
        }

        const double invPivot = 1.0 / a[col][col];
        for (int j = 0; j < n; ++j) {
            a[col][j] *= invPivot;
            inv[col][j] *= invPivot;
        }

        for (int row = 0; row < n; ++row) {
            if (row == col) {
                continue;
            }
            const double factor = a[row][col];
            for (int j = 0; j < n; ++j) {
                a[row][j] -= factor * a[col][j];
                inv[row][j] -= factor * inv[col][j];
            }
        }
    }

    return inv;
}

template<int n, int m>
inline std::ostream& operator<<(std::ostream& out, const mat<n, m>& m_) {
    for (int i = 0; i < n; ++i) {
        if (i != 0) {
            out << '\n';
        }
        out << m_.rows[i];
    }
    return out;
}

inline vec4 transform_point(const mat4& m, const vec3& v) {
    return m * vec4(v, 1.0);
}

inline vec3 transform_direction(const mat4& m, const vec3& v) {
    const vec4 r = m * vec4(v, 0.0);
    return vec3(r.x, r.y, r.z);
}

inline vec3 transform_normal(const mat3& m, const vec3& n) {
    return normalize(m * n);
}

template<int n>
inline std::istream& operator>>(std::istream& in, vec<n>& v) {
    char trash;
    for (int i = 0; i < n; ++i) {
        if (i != 0) {
            in >> trash;
        }
        in >> v[i];
    }
    return in;
}
