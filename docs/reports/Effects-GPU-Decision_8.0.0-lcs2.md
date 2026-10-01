# GPU hosting experiment: integration decision

Date: 2026-10-01. Status: CLOSED / NO-GO for main. The offline observer and
ownership correction passed; one native cleanup invocation released the
observed window handles and client owners in-process, but auxiliary-Space
lifetime remains unknown. No A/B or visual qualification followed.

Production comparison target is f33d1acad2ef17388888d3b5432cc34374fc43bf /
lcs.2. Experimental base is
93feb9b; the experiment's existing uncommitted work and default-OFF build
remain preserved on the experiment branch
`lcs-code/effects-buffer-gpu-presentation`.

## Integration decision

**No GPU change is to be integrated into the main renderer.** A standalone
blit engine without a qualified presentation consumer and measured end-to-end
benefit does not solve a demonstrated production problem. The new observer
corrects diagnostic inference, not production rendering. The completed native
slot settled the observable window-handle/client-lifetime boundary; it did not
remove the declared auxiliary-Space observability limit or establish a
performance win.

| Category | Candidate | Actual problem addressed and evidence | Dependency on f33d1a/lcs.2 and disposition |
| --- | --- | --- | --- |
| (a) Integrable | Decision/provenance documentation only, after maintainer review | Corrects legacy source confound, incomplete cleanup inference and unsupported promotion claims | Independent of runtime. Selected report may be incorporated; no mass raw/harness import and no old-base cherry-pick |
| (b) Experimental only | BGEngine BGRA/IOSurface import, texture-cache/wrapper ownership and one-job slot | Byte-exact GPU blit and safe source retention; historical 66+4 offscreen readbacks and sanitizers | Current renderer uses CGImage/raster surface. Needs a new consumer/capture adapter and measured cost before any daemon use; retain tools/default OFF |
| (b) Experimental only | BGPresentation A/B controller and pure readiness/cancel gate | Explicit Apple transaction ordering, generation/deadline/cancel and late-callback ownership, tested with doubles | Current lcs.2 uses an initialized raster guard and capture admission 150 ms. Controller is not a drop-in snapshot/discard adapter or valid navigation integration |
| (b) Experimental only | Conservative cleanup classifier, native observer and scoped-release helper | Prevents missing/ambiguous bounds/type/membership from becoming cleanup passes; real trace replay red-to-green and focused lifetime checks | Native server release is distinct from client lifetime. Private observer semantics are not production guarantees; diagnostic helper only |
| (b) Experimental only | Focused readback, driver, cleanup/replay tests and selected ledger | Preserves precise evidence and reusable regression seams | No production consumer; keep local tools/tests behind OFF rather than importing broad CMake/debug harness |
| (c) Set aside for this integration | Covered dual-window/auxiliary-Space Metal host as renderer replacement | Presentation was intermittent in historical selected cases; auxiliary-Space retirement cannot independently be verified by current APIs | Retains CPU raster copy and second window. Does not establish reliable exposed pixels, continuity or benefit over current guard. Not admissible to main |
| (c) Set aside | Colored synthetic fixtures and broad ordering/event/timeout variants | Diagnostic patterns explained test-only rectangles; late-order source provenance was confounded | No production remedy demonstrated. Preserve raw provenance, do not import flags or repeat 75-case sweep |
| (d) Not assessed | Exposed full-display B equivalence/continuity, end-to-end UX, multi-display/HDR/cursor behavior | Conditional gates require qualified native cleanup; not inferred from presentedTime | Blocks renderer promotion; absence of these tests is explicit, not an offline pass |
| (d) Not assessed | GPU memory/speedup/zero-copy replacement and native navigation adapter | No matched production consumer/cost benchmark in this bounded campaign | Capture/source API and initialized visible backing would need independent design. No extra campaign is part of this decision |

## Scope and necessary remaining question

The original A is counted but cannot be reused as a paired control after setup
changes. The ledger and final native observations below close the one initial
slot. The explicit revised cap is 12 total: consumed A (1) + cleanup (1) +
conditional 4 pairs (8) + static (1) + cancel (1). Four pairs would be
diagnostic, not the original 6/6 acceptance. Currently used Space type and
membership APIs cannot prove auxiliary-Space destruction; unknown blocks all
conditional stages. A no-go/incomplete result closes this branch without
searching speculative private APIs or spending the unused budget.

At most one independent next performance direction is supported: isolate capture
and raster preparation cost in the existing lcs.2 renderer with a matched
headless/client-alive control before changing renderer technology. Existing
reports motivate that boundary; this report does not run that benchmark or
promise a gain. GPU rendering in general is not ruled out by this host-specific
no-go.

Related material:

- Frozen plan and offline logs: local build output, not published.
- Historical evidence and corrections: the experiment-branch report
  `docs/reports/effects-buffer-gpu-presentation-2026-09-30.md`.
- Review: [Effects GPU review](Effects-GPU-Review_8.0.0-lcs2.md).

## Final ledger: cumulative count preserved

| Attempt/evidence | Actual native host invocations | Result |
| --- | --- | --- |
| 2026-09-30 frozen pair 1-A admission | 0 | third-party application windows in the scene, refused; no host |
| 2026-10-01 first fresh admission | 0 | Mouse-idle refusal before switch, no host |
| 2026-10-01-ready2 original A | 1 | Positive readiness 284.569 ms; old teardown observer failed; raw preserved |
| New cleanup native/ admission | 0 | third-party application windows on the empty target, refused; no host; restored without interference |
| Fresh native cleanup run | 1 | Native process-alive observations below; cleanup gate unknown |
| Revised 4-pair A/B diagnostic stage | 0 | Not run by cleanup gate, B not evaluated |
| Conditional static full-display and cancel/lifecycle | 0 | Not run by cleanup gate |
| **Total under original cap** | **2 of maximum 12** | **10 slots unspent; campaign closed, no reset** |

The original A remains separate and is not a matched control after setup
changes. The new cleanup uses mode A only to prepare one immutable initialized
frame; it is not a second accepted A/B pair. The historical 75 readiness trials
(49 ready, 26 timeouts) predate this bounded comparison, remain separate
selected-case data, and were not rerun or relabeled. Native/refused outputs and
frozen plans are retained in all prior directories. No trial was replaced
silently.

## Native observation with process alive

After the third-party application windows were closed and the mouse was reconfirmed stationary, target Desktop
passed metadata-only privacy admission with no foreign windows. Fresh current
display ID 2 (6016x3384, logical 3008x1692, rotation 0), origin Space 3 and
empty target 5 were used, not earlier IDs. The native diagnostic process is the
same in all five observations. Installed binary/PID and managed topology
remained equal within this invocation; origin restored with no HID
interference. No capture or pixel recording occurred. Temporary inhibitor and
host ended; no remaining matching process was found in the final process check.

GPU completed successfully; direct and callback-fed presentedTime agreed at
7924.189773625 host seconds. Readiness observation was 296.803 ms from host
start, within fixed 3 s diagnostic and existing 1 s host ceiling. Source decode
was 244.961 ms separately, outside host budget. These selected timings are
observations including initialization/polling, not GPU execution costs, a
production benchmark or UX acceptance. No speedup is established.

| Observation phase | Raw guard | Native Metal | Client objects alive | From visible-close start |
| --- | --- | --- | --- | --- |
| before_visible_close | visible_ordered | visible_ordered | window,layer,host,job | before close |
| after_visible_close_owners_held | hidden_registered | visible_ordered | window,layer,host,job | 7.085 ms |
| after_resource_release_before_pool | hidden_registered | visible_ordered | window,layer,host,job | 16.968 ms |
| after_actual_owner_and_pool_release_process_alive | released | visible_ordered | none | 23.685 ms |
| final_in_process_observation | released | released | none | 41.948 ms |

The known-valid owned handles and invalid ID control validated the composite
window predicate. Invalid bounds returned 1000/CGRectNull, absent public
inventory and unordered state; ordered query returns 0 even for absent control,
so its return code alone is not an existence predicate. Final guard 5812 and
Metal 5813 matched absent-control bounds/error/null state, were absent in public
inventory and unordered. Weak window/layer/host/job markers were nil, work
resolved, close and raw guard release acknowledged 0. Both observed
handles/client owners were released after 41.948 ms within the unchanged
100 ms, while the diagnostic process was alive. Process exit was not used as
proof or as the mechanism of that successful observation.

The observer correction therefore resolves the OLD READBACK QUESTION for this
new invocation: observations before real owner/pool release are not a final
retirement check. Raw guard also remained registered before pool drain, so an
AppKit-window-only explanation is insufficient. After actual owner/pool release
the guard disappeared first; the native-window handle disappeared after bounded
event dispatch. These are direct staged facts. They do not uniquely attribute
private server internals to an autorelease, a cache or another retention path;
setup also now retires actually completed GPU work before dropping owner refs.
No broad variants were used to assign unsupported single-factor causality.

An additional boundary is visible in those same readbacks: hiding the guard and
requesting orderOut on the native window are not simultaneously observed. The
native window still read ordered after the guard was hidden and even in the
first after-pool sample. No intentional reveal/visual gate was requested, but
these facts cannot guarantee that no Metal pixel was exposed briefly during
closure. No pixel recording was made, so actual exposure/continuity is unknown.
The source/job had already reached positive presentation with matching geometry;
that still does not prove pixel equivalence or atomic coupled window removal.
This limitation is retained, not silently described as a continuously covered
successful visual test.

## Why cleanup still does not qualify and campaign closes

Space destroy returned 0, membership dropped from 2 to 0, but Space type and
invalid-ID type were both 3 before and after. Membership empty proves neither
Space absence nor destruction. Currently used APIs provide no independent
auxiliary-Space existence enumeration. Consequently space_state remains unknown
and space_enumeration_validated=false, exactly as declared before execution;
cleanup_qualified=false / host_rc=2 is the honest overall verdict. No Space
leak is asserted. Public handle disappearance does not certify every internal
server allocation either. The existing installed renderer is not newly
diagnosed as faulty merely from this experimental observability gap.

The necessary integration question is settled negatively: a replacement host
has no validated full lifecycle/visibility contract and no demonstrated useful
consumer/cost advantage over lcs.2. B's presentation contract itself is NOT
rejected (B never ran); the covered host is not admissible to main. The
remaining question about auxiliary-Space internals and original intermittent
presentation would require a separately justified architecture investigation.
It blocks promotion, not this decision. This campaign closes now by its frozen
safety criterion and bounded scope, rather than spending the 10 unused slots on
another sweep or producing a generic request for more tests.

## Verification and review artifacts

- Actual failed cleanup trace replay: extracted legacy inference red, corrected
  real shared classifier green with all states unknown for missing evidence.
- New Debug semantic/scoped-lifetime tests 2/2; ASan/UBSan 3/3 and TSan 3/3
  include presentation cancellation/late-callback driver regressions. Mock/weak
  results were not treated as native cleanup proof.
- Strict build of host/observer/core helpers passed; analyzer 3/3 clean.
  Existing restore runner 2/2 and protocol 5/5, new cleanup decision criteria
  5/5 passed.
- Actual BGHost.close seam also passed ASan/UBSan offline with private mutators
  replaced: empty/double close, hide distinct from release, no duplicated
  destruction, first release error 1000 preserved across another close, client
  owner detach. This creates no NSApplication/window/Space/GPU and is not a
  native server claim. Saved as close-seam-tests.m / close-seam.log in the local
  build dossier; it did not change the frozen native source or executable.
- Unchanged engine/presentation source hashes and earlier 66+4 real offscreen
  GPU readbacks reused; no broad suite, performance or memory campaign repeated.
- Source, executable, plan, immutable fixture and retained original raw hashes
  matched before and after native dispatch. CMake default OFF; no daemon/payload
  source changed. ARC isReleasedWhenClosed remains false.

The final raw result, native trace, frozen plan, refused-admission record,
offline logs and review patch are local build output and are not published.
