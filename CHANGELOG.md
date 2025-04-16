# Changelog

## [0.2.1](https://github.com/gpac/gst-gpac-plugin/compare/v0.2.0...v0.2.1) (2025-04-16)


### Features

* add `m2tsmx` filter subelement ([803fbec](https://github.com/gpac/gst-gpac-plugin/commit/803fbec07abd91a24bf50d8f6b54ec33917d2a8d))
* slice data buffer into individual samples for fMP4 ([ea3c003](https://github.com/gpac/gst-gpac-plugin/commit/ea3c003cca94e0f9e38b520c9ac0b75941b2f8f7))


### Bug Fixes

* handle multiple sink pads correctly ([17f8a18](https://github.com/gpac/gst-gpac-plugin/commit/17f8a183f58a369562cf9470f5c479d9e2aecd80))
* handle non-fragmented mp4 ([b55c6c0](https://github.com/gpac/gst-gpac-plugin/commit/b55c6c09f076e4e9a325a301f4af1f38e14c3285))
* set dsi once ([e74057a](https://github.com/gpac/gst-gpac-plugin/commit/e74057a53b6e8b3fd3c2310500e5c88640f7c010))
* update debug logging format for sample size in mp4mx ([5df7609](https://github.com/gpac/gst-gpac-plugin/commit/5df7609ae3582c51bdc3a90a81d66875f8e44562))

## [0.2.0](https://github.com/gpac/gst-gpac-plugin/compare/v0.1.2...v0.2.0) (2025-02-22)


### ⚠ BREAKING CHANGES

* rename `gpacmp4mx` to `gpaccmafmux`

### Features

* add segment duration signaling ([13e7948](https://github.com/gpac/gst-gpac-plugin/commit/13e794845fe8d4e466019ca2bc9422c8472c485d))
* allow overriding default options for single filters ([9a55973](https://github.com/gpac/gst-gpac-plugin/commit/9a5597393dfac4fa6f953b3caadac198da754e90))
* rename `gpacmp4mx` to `gpaccmafmux` ([54280a6](https://github.com/gpac/gst-gpac-plugin/commit/54280a616dbfffaa4304dcdca0e1165ea72fc3df))


### Bug Fixes

* correct argument allocation and NULL termination in gpac_apply_properties ([6a93ba7](https://github.com/gpac/gst-gpac-plugin/commit/6a93ba73f7daab51489bcc52b407f6759ac4f942))
* deduplicate reconfigure requests ([a356f04](https://github.com/gpac/gst-gpac-plugin/commit/a356f0444316ee0cdc784522ab4a88f98e47a404))
* handle live input properly ([#5](https://github.com/gpac/gst-gpac-plugin/issues/5)) ([ba378b2](https://github.com/gpac/gst-gpac-plugin/commit/ba378b258ac733859792f734a06cf2d21b714d9c))
* more robust idr requests ([5e5da91](https://github.com/gpac/gst-gpac-plugin/commit/5e5da9128c87258c60e745f52aa47f47db626a2c))
* properly transition through states ([05c0c20](https://github.com/gpac/gst-gpac-plugin/commit/05c0c205954dbc89b605a4cd7122449d78361d17))
* regression from 05c0c20 ([2ada29d](https://github.com/gpac/gst-gpac-plugin/commit/2ada29d9b5de223657c52918d88ddfcfa54e4755))
* regression from 5e5da91 ([5727042](https://github.com/gpac/gst-gpac-plugin/commit/57270422f2940cf6d91f6cea833355da0874fb76))
* wrong error handling on register ([ddfd148](https://github.com/gpac/gst-gpac-plugin/commit/ddfd148113f36f5c101a4c6c47e3b61cdc6276f2))

## [0.1.2](https://github.com/gpac/gst-gpac-plugin/compare/v0.1.1...v0.1.2) (2025-02-09)


### Features

* drive the encoder via encode_hints events from gpac ([70293e3](https://github.com/gpac/gst-gpac-plugin/commit/70293e38e0332966800d8a847ea9536073ee5fad))


### Bug Fixes

* add more context around failures ([026cc9d](https://github.com/gpac/gst-gpac-plugin/commit/026cc9d5ccf21e5434c8a57dd73b8c4dca9f9d40))
* ensure IDR period is valid before sending key frame request ([4fd49f6](https://github.com/gpac/gst-gpac-plugin/commit/4fd49f69c791662305d6a44ed74866796c561d21))
* flush the gpac packets on every aggregate call ([3631699](https://github.com/gpac/gst-gpac-plugin/commit/363169910cbbc1fc1632ec941dde9f392a48b296))
* GST_ELEMENT_* argument ordering ([77a5c01](https://github.com/gpac/gst-gpac-plugin/commit/77a5c01a2c01bfde22b3ee887bfd94d805879f49))
* more robust box parser for mp4mx post-processor ([487882b](https://github.com/gpac/gst-gpac-plugin/commit/487882ba2b27ae3ad0768c4224accbebb38d6d76))
* n&gt;0 segments should indicate delta unit on headers ([23fe47b](https://github.com/gpac/gst-gpac-plugin/commit/23fe47b65c5965cb13d7682ea68d66617f14579c))
* reduce unnecessary idr warnings ([b20efa7](https://github.com/gpac/gst-gpac-plugin/commit/b20efa7958e7843c21772b647dc41ccb122d20a0))
* remove unnecessary copies ([ad2257b](https://github.com/gpac/gst-gpac-plugin/commit/ad2257b983385c0ed08de70c9e7fa0752fb924b7))
* set pck ref before creating the memory ([e7f01fb](https://github.com/gpac/gst-gpac-plugin/commit/e7f01fb8b2eb287b7f0fa97632d52e3082780592))

## [0.1.1](https://github.com/gpac/gst-gpac-plugin/compare/v0.1.0...v0.1.1) (2025-02-02)


### Features

* add `print-stats` option ([d81d1aa](https://github.com/gpac/gst-gpac-plugin/commit/d81d1aab59208d75d7030c09a18fe743a6d6d364))
* semi-aligned mp4mx output ([a50ca08](https://github.com/gpac/gst-gpac-plugin/commit/a50ca0830173a3f41cd709c0a9a499d7f79f58cc))


### Bug Fixes

* account for segment updates in dts_offset ([2a481c5](https://github.com/gpac/gst-gpac-plugin/commit/2a481c52b2ea6a4ccf9213c732b0dd844d891978))
* make mp4mx output cmaf by default ([1536d05](https://github.com/gpac/gst-gpac-plugin/commit/1536d05336041838e0d8f09f6f87060881ca1ba2))
* misc warnings and errors on macOS ([845edd7](https://github.com/gpac/gst-gpac-plugin/commit/845edd75a353fda90a6ae6c66e7ef3764c18ba65))
* parse gpac filter options correctly ([ca55868](https://github.com/gpac/gst-gpac-plugin/commit/ca5586865be23953171470d605336b4a13b041da))
* reconnect filter outputs on caps change ([bdbacb6](https://github.com/gpac/gst-gpac-plugin/commit/bdbacb6152d5eb7cf988bfd0d50662c91ba1109d))
* segfault on early session close ([9347ee8](https://github.com/gpac/gst-gpac-plugin/commit/9347ee894f93ce21740cb9fb5890e348236eb0fb))
* set buffer timestamps as correct as possible ([d763a01](https://github.com/gpac/gst-gpac-plugin/commit/d763a019c695b836106236bb867f0c47c512291c))
