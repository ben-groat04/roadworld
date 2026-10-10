# City Painter: v0 pre-research findings

10 Oct 2026

Checks done before handing v0 to Claude Code, so its limited usage goes on building. Items marked unchecked still need verifying. This is not legal advice.

## Findings that change the plan

- **Avoid CGAL.** Most of its algorithm packages are GPL, including Straight_skeleton_2. Only the foundation packages are LGPL, and a commercial licence is sold by GeometryFactory. Epic's FAQ says Unreal Engine code cannot be combined with copyleft-licensed code, so a shipped game cannot casually use GPL CGAL. Use **Clipper2** (Boost Software Licence, permissive) for polygon clipping, offsetting and triangulation. Boost.Geometry is the other candidate (its licence is unchecked).
- **A straight skeleton for roofs** therefore needs a permissive alternative or a custom implementation. This is not needed for v0.
- **Unreal may cover vegetation.** Unreal 5.7 (released late 2025) marked PCG as production-ready, and it includes an experimental Procedural Vegetation Editor and experimental Nanite Foliage. This could reduce the need for Infinigen. Compare both when vegetation arrives. Whether a newer engine version exists is unchecked.

## Unreal licensing (checked against Epic's licence page)

- Free for the first $1 million of lifetime gross product revenue, then a 5% royalty on revenue above that.
- Epic Games Store sales are royalty-free.
- A Release Notification Form is required when you ship, then quarterly royalty reports.
- No combining engine code with copyleft code (see CGAL above).

## Real UK data for test fixtures

- **Environment Agency LIDAR Composite DTM, 1 m:** covers about 99% of England, delivered as GeoTIFF in 5 km tiles on the OS National Grid, under the Open Government Licence with an attribution statement. It covers England only. Vertical accuracy is about ±15 cm.
- **OpenStreetMap (ODbL):** the OSM licence FAQ says the ODbL restricts only public use, so internal test fixtures are fine. If a shipped game includes OSM-derived maps, they count as a produced work needing attribution, and recipients can request the derived database.
- **Ordnance Survey open data:** licence and coverage unchecked.

## Road markings: the official sources

- **Traffic Signs Manual Chapter 5: Road Markings** (Department for Transport, 2019 edition, free PDF): https://assets.publishing.service.gov.uk/media/5c4ace6ded915d38a0611abc/traffic-signs-manual-chapter-05.pdf. Topics include stop and give way markings, longitudinal lines, double white lines, major and minor junctions, roundabouts, signal-controlled junctions, grade-separated junctions, yellow box junctions, traffic islands, pedestrian crossings, waiting restrictions, arrows and lane destinations. The manual says to check gov.uk for the current edition.
- **Traffic Signs Regulations and General Directions 2016, Schedule 9, Part 6:** the regulatory road markings, on legislation.gov.uk (verify the current amended version).
- **Chapter 3 (Regulatory Signs)** covers stop and give way, cycle and bus markings, which Chapter 5 defers to.
- **Highway Code road markings page** on gov.uk is a quick reference.

## Still to do

- Marking rules: first extraction of Chapter 5 sections 1 to 5 is done in the doc City Painter: UK road marking rules (draft, sections 1 to 5), exported as docs/road-marking-rules.md. No further extraction is planned: it is a reference, and the person tunes exact numbers by eye.
- Check Ordnance Survey open data, Runtime Virtual Texturing status, Boost.Geometry's licence, and the current Unreal version.
