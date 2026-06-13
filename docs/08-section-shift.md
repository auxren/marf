# 8. Section shift (un‑expanded units)

In normal operation both generators use the **same** stage programming — the
same 16 stages — exactly as the original 200‑series module does. On an
un‑expanded unit, a *section shift* lets you point each generator at one of two
banks of 16 stages, giving you access to stages **17–32** without an expander.

## How to shift

| Action | Result |
|--------|--------|
| **Display 1 + Stage No Right** | Move **AFG 1** to stages **17–32** |
| **Display 1 + Stage No Left** | Move **AFG 1** back to stages **1–16** |
| **Display 2 + Stage No Right** | Move **AFG 2** to stages **17–32** |
| **Display 2 + Stage No Left** | Move **AFG 2** back to stages **1–16** |

(Hold the Display button for that generator and tap the Stage No direction.)

## What shifts and what doesn't

- The **stage programming** that the generator reads shifts to the other bank.
- The **physical sliders are still the 16 on the panel.** When a generator is
  playing stages 17–32, the slider values it uses are still those of sliders
  1–16. In other words, the *switch programming* is independent per bank, but the
  *slider levels* are shared.

## With an expander

If an expander is connected (and DIP switch 4 is on), the module has a full
**32 stages** with their own sliders, and the section‑shift trick is not used —
the Display + Stage No combinations have no shifting effect.
