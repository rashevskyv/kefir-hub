#pragma once

// Chromium NX (github.com/shodowlo/chromium-nx): the Chromium browser as a homebrew. Experimental.
// Kefir Hub downloads its NRO and starts it; the NRO's own launcher gets its image and its updates.

namespace sphaira::chromium_nx {

// warns (experimental, full memory), downloads the NRO when it is missing, starts it.
void Open();

} // namespace sphaira::chromium_nx
