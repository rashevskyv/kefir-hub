#pragma once

#include <cstddef>

namespace sphaira::ui::menu::dbi {

class PackageSkipGate {
public:
    void Open(size_t package) {
        m_package = package;
        m_open = true;
        m_accepted = false;
    }

    bool Accept(size_t package) {
        if (!m_open || m_accepted || package != m_package) {
            return false;
        }
        m_accepted = true;
        return true;
    }

    bool Close() {
        const bool accepted = m_open && m_accepted;
        m_open = false;
        return accepted;
    }

private:
    size_t m_package{};
    bool m_open{};
    bool m_accepted{};
};

} // namespace sphaira::ui::menu::dbi
