*This page is also available in [French](README.md).*

# Toy backend and toy screen — fork how-to

This directory is the proof, and the template, that the core
(`firmware/main/core/` and `firmware/main/ui/`) can host an application that is
not Klipper without modifying a single line of it. The astro fork starts here.

## The four files to write

| File | Contract fulfilled | Contents |
|---|---|---|
| `backend_jouet.h` / `.c` | `backend_desc_t` (`core/backend.h`) | A state with two fields (`compteur`, `libelle`); `rafraichir()` increments `compteur`; `commande()` handles the `"reset"` action. No I/O. |
| `ecran_jouet.h` / `.c` | `ecran_desc_t` (`ui/ecran.h`) | A title, the counter value, and a Reset button that calls `ui_commander("reset", NULL)`. |

Contract points that really matter, verified by breaking them while writing
this backend (see `host-test/tests/test_jouet.c` for the tests that pin them
down):

- **The `etat` buffer received by `rafraichir()` is zeroed by the core before
  EVERY call** (`core/backend.h`, comment on `rafraichir`). A counter that must
  survive from one cycle to the next is kept in a file-level `static` variable
  (see `g_compteur` in `backend_jouet.c`), never by reading it back from
  `etat`. This is exactly the bug that cost the most in the previous milestone
  (see `backend_factice.c`, CRITICAL 1 comment).
- **`demarrer()` must not write anything into `etat` beyond zero** if you want
  `generation` to stay at 0 right after startup (the "before" buffer is also
  zeroed at that point; writing anything into it already advances `generation`
  before the first real `rafraichir()` — RED observed while writing this
  backend, see `backend_jouet.c`).
- **`commande()` returns `ESP_ERR_NOT_SUPPORTED` on an unknown action**, never
  a silent success — the interface must be able to grey out a button knowingly
  (`core/backend.h`).
- **`mettre_a_jour()` greys out on `donnees_perimees=true`, systematically on
  every call** (never incrementally) and never puts up its own network error
  box (specification §5.3, only the shell displays the link state).
- **A synchronous failure of `ui_commander()`** (queue full, loop not started)
  is reported through `habillage_notifier(..., true)` — never an error box put
  up by the screen. Copy the pattern from
  `firmware/main/apps/klipper/ecrans/ecran_accueil.c::executer_commande()`.

## The two registration lines

At the application's assembly point (simulator side: `simulateur/main.c`;
device side: your own `app_main.c`, never `core/` nor `ui/`):

```c
navigation_empiler(&ECRAN_JOUET);
/* ... */
source_etat_sim_demarrer(backend_jouet_desc());   /* ou boucle_demarrer() sur cible */
```

That is all. Nothing else to touch in `core/` or `ui/` for the screen to be
displayed, to update itself, and for its button to send a command.

## The only real snag: `ui/habillage.c`

`habillage_pomper()` (called on every frame/cycle to refresh the status bar AND
to relay the application state to the visible screen) carries a **concrete**
`etat_klipper_t` buffer — not a generic one — in order to read the state via
`ui_etat_instantane()`. This coupling is **documented and accepted** at the top
of `ui/habillage.h`: *"A fork with a different application (a non-Klipper
machine) adapts that file at both sites, not ecran.h/navigation.c, which stay
reusable as they are."* The second site is
`libelle_commande()`, which translates Klipper's `BACKEND_ACTION_*` actions
into English words for the notification banner.

Two ways to deal with this, depending on your situation:

1. **Real fork (recommended): adapt `ui/habillage.c`.** Replace the
   `etat_klipper_t g_etat;` buffer with your application's state type (or with
   a `uint8_t[]` of the right size if your app changes backend at runtime), and
   adapt `libelle_commande()` to your own actions. This is the path that the
   header comment of `habillage.h` explicitly anticipates — you are not
   inventing anything, you are following the written contract.

2. **"Zero lines in `core/`/`ui/`" constraint (the case of task 11 itself, not
   that of an ordinary fork): bypass `habillage_pomper()` for the application
   state relay, while keeping the call for the status bar.**
   `ui_etat_instantane()` (generic façade, `void*`/size),
   `habillage_donnees_perimees()` and `navigation_mettre_a_jour()` are all
   three PUBLIC and already generic functions of `ui/` — they are exactly the
   three calls that `habillage_pomper()` already makes internally. Calling them
   yourself from your assembly point, with a buffer the size of your own state,
   closes the loop without modifying `ui/`:

   ```c
   static void jouet_pomper(void)
   {
       etat_jouet_t etat;
       uint32_t generation;
       liaison_etat_t liaison;
       if (ui_etat_instantane(&etat, sizeof(etat), &generation, &liaison)) {
           navigation_mettre_a_jour(&etat, habillage_donnees_perimees(liaison));
       }
   }
   ```

   See `simulateur/main.c` (`jouet_pomper()`) for the actual implementation,
   called alongside `habillage_pomper()` on every cycle. The price of this
   option: the status bar's connection pill (its text and its colour) stays
   stuck on "connecting"/grey for your application, because the *internal* call
   that `habillage_pomper()` makes to `ui_etat_instantane()` fails its own size
   check (the buffer is an `etat_klipper_t`, never yours) and therefore never
   fills its local link variable. The rest of the bar (title, time, wifi,
   battery) stays correct: nothing else there depends on the size of the
   application state. Full details with supporting screenshots: review of task
   11, milestone 2b.

An ordinary fork has no reason to live with this defect: option 1 fixes it in
three lines. Option 2 only exists because task 11 literally measures
`git diff --stat -- firmware/main/core firmware/main/ui` and it must stay
empty.
