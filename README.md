# GStreamer GPAC Plugin

![GitHub Release](https://img.shields.io/github/v/release/gpac/gst-gpac-plugin?style=for-the-badge)
![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/gpac/gst-gpac-plugin/test.yml?style=for-the-badge&label=tests)

This plugin provides elements that interface with the GPAC library, bringing advanced multimedia processing capabilities of GPAC to GStreamer pipelines.

This plugin is dual-licensed AGPL or commercial. Contact [Motion Spell](https://www.motionspell.com/contact/) for more information.

## Elements

### `gpactf` element

The `gpactf` element is an aggregator element that runs the incoming buffers through the GPAC Filter Session. The element provides a `graph` option for you to specify the GPAC filter graph to use. Make sure to read the [Filters Concepts](https://wiki.gpac.io/Filters/filters_general/) page on the GPAC wiki to understand how to create filter graphs.

> [!NOTE]
> You can assume that source and sink filters are already present in the graph. You will be populating the graph in between these two filters.

### `gpacmp4mx` element

Functions similarly to `gpactf` element, you can assume it's equivalent to `gpactf graph=mp4mx`. Only difference is on how the element is configured. You can use the element options to set the `mp4mx` configuration.

### `gpacsink` element

This is again a convenience element that functions similarly to `gpactf` element, except that it's contained under a `GstBin` and connected to `fakesink`. This element is useful for testing purposes.

## Installation

The plugin requires GPAC to be installed on your system. You can install GPAC by following the instructions on the [GPAC wiki](https://wiki.gpac.io/Build/Build-Introduction/). There's no specific required version of GPAC, but we recommend building from source to ensure compatibility. You can also find the latest build artifacts for the plugin [here](https://github.com/gpac/gst-gpac-plugin/releases/latest). Be sure to rename the library files to `libgpac_plugin.{so,dylib}` and place them in the appropriate location.

## Build

The plugin requires GPAC to be installed on your system. You can build GPAC from source by following the instructions on the [GPAC wiki](https://wiki.gpac.io/Build/Build-Introduction/). The plugin also requires GStreamer headers and libraries to be installed on your system. You can follow the instructions on the [GStreamer website](https://gstreamer.freedesktop.org/documentation/installing/index.html?gi-language=c) to install GStreamer.

To build the plugin, follow these steps:

```bash
cmake -S . -B build
cmake --build build
```

## Usage

Refer to the launch tasks in [`.vscode/launch.json`](.vscode/launch.json) for examples of how to use the plugin. Each launch configuration builds the plugin and runs a GStreamer pipeline that utilizes it. After the session is completed, the pipeline graphs are dumped to `graph` folder.

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on how to contribute to this project.

## License

This project is dual-licensed AGPL or commercial. Contact [Motion Spell](https://www.motionspell.com/contact/) for more information.
