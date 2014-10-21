**pngatls** is a simple texture atlas packer.

##### Features

- [MaxRects](http://clb.demon.fi/files/RectangleBinPack.pdf) bin packing algorithm
- single or multiple file output
- frame data embedded as `atLS` chunks, allowing repacking

##### Usage

```
usage: pngatls [-t] [-p pad] [-m size] [-x .xml] [-j .json] atlas.png in.png ...
       pngatls -e atlas.png ...
```

##### Examples

Pack single file, as big as required, trim transparency (`-t`):

    pngatls -t atlas.png sprite*.png

Pack series of 4096×4096 textures beginning with `texture00001.png`:

    pngatls -t -m 4096 texture.png sprite*.png

Repack two atlases into series of textures and write companion JSON files:

    pngatls -m 4096 -j texture.json texture.png tree_atlas.png grass_atlas.png

Extract frames into current directory, restoring trimmed transparency:

    pngatls -e texture*.png
