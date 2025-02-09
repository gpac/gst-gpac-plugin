# Changelog

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
