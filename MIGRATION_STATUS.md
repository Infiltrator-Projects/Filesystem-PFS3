# PFS3 local source consolidation — 6 November 2026

All 53 tracked files under `native/filesystems/pfs3/` from central Filesystems main `a253fb724ce1fd760a361219281effb6abd71367` are now present in this independent repository. The existing portable engine was identical and preserved, while the native Linux driver adapter, retained original PFS3-AIO implementation, and Windows adapter README were imported verbatim, including the original license and notices.

The `.so` build uses the independent canonical source. The `.ko` packaging now uses local `native/filesystems/pfs3` source instead of downloading an archived Filesystems snapshot. Release and kernel runtime qualifications remain independently required before removing the central build inputs and their CI tests.
