# Mongoose

Vendored Mongoose source used by IoTEdgeGateway.

- Version: 7.21
- Source mirror used for this workspace: https://gitee.com/mirrors/mongoose
- Upstream: https://github.com/cesanta/mongoose

Only these files are required by the build:

- `mongoose.c`
- `mongoose.h`

The build links `mongoose.c` into the `mongoose_lib` static library directly, so
no separate `make` or installation step is required.
