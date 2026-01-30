#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>

/*C implementations for tile-related hotspots*/

#define TILE_WIDTH  8
#define TILE_HEIGHT TILE_WIDTH
#define TILE_SIZE   64

#define TILE_OFFSET(off_x, off_y) (TILE_WIDTH * (off_y) + (off_x))

#define TILE_BOTTOM_RIGHT_IN_BUFFER(tx, ty, bw) \
	((bw) * ((ty) + TILE_WIDTH - 1) + ((tx) + TILE_WIDTH - 1))
#define TILE_FITS_ON_BUFFER(tx, ty, bw, bs) \
	(TILE_BOTTOM_RIGHT_IN_BUFFER(tx, ty, bw) < (bs))

#define ASSERT(cond, msg) \
	if (!(cond)) { \
		PyErr_SetString(PyExc_ValueError, msg); \
		return NULL; \
	}


// _4bpp_to_8bpp(input : bytes) -> bytes
static PyObject *_4bpp_to_8bpp(PyObject *self, PyObject *args) {
	const unsigned char *input;
	Py_ssize_t input_len;

	if (!PyArg_ParseTuple(args, "y#", &input, &input_len)) return NULL;

	char *output = PyMem_Malloc(2 * input_len);
	int j = 0;
	for (int i = 0; i < input_len; i++) {
		output[j++] = input[i] & 0xF;
		output[j++] = input[i] >> 4;
	}

	return PyBytes_FromStringAndSize(output, 2 * input_len);
}


// _8bpp_to_4bpp(input : bytes) -> bytes
static PyObject *_8bpp_to_4bpp(PyObject *self, PyObject *args) {
	const unsigned char *input;
	Py_ssize_t input_len;

	if (!PyArg_ParseTuple(args, "y#", &input, &input_len)) return NULL;

	char *output = PyMem_Malloc(input_len / 2);
	int j = 0;
	for (int i = 0; i < input_len / 2; i++) {
		output[i] = input[j++] & 0xF;
		output[i] |= input[j++] << 4;
	}

	return PyBytes_FromStringAndSize(output, input_len / 2);
}


// flip_tile_data(input : bytes, hflip : bool, vflip : bool) -> bytes
static PyObject *flip_tile_data(PyObject *self, PyObject *args) {
	const unsigned char *input;
	Py_ssize_t input_len;
	int hflip, vflip;

	if (!PyArg_ParseTuple(args, "y#pp", &input, &input_len, &hflip, &vflip))
		return NULL;

	ASSERT(input_len == TILE_SIZE, "Tiles must be 64 bytes long.");

	char *output = PyMem_Malloc(TILE_SIZE);
	for (int y = 0; y < TILE_WIDTH; y++) {
		for (int x = 0; x < TILE_WIDTH; x++) {
			int x2 = hflip ? ((TILE_WIDTH - 1) - x) : x;
			int y2 = vflip ? ((TILE_WIDTH - 1) - y) : y;
			output[TILE_OFFSET(x2, y2)] = input[TILE_OFFSET(x, y)];
		}
	}
	return PyBytes_FromStringAndSize(output, TILE_SIZE);
}


// read_ncbr_tile(data : bytes, tilenum : int, bpp : int, width : int) -> bytes
static PyObject *read_ncbr_tile(PyObject *self, PyObject *args) {
	const unsigned char *data;
	Py_ssize_t data_len;
	unsigned int tile_index, bits_per_pixel, tilemap_width;

	if (!PyArg_ParseTuple(args, "y#III", &data, &data_len, &tile_index,
			&bits_per_pixel, &tilemap_width))
		return NULL;

	int tile_width_length = (TILE_WIDTH * bits_per_pixel) / 8;
	int tile_width_stride = tile_width_length * tilemap_width;
	int tile_width_skip = tile_width_stride - tile_width_length;

	int tile_height_length = tile_width_stride * TILE_HEIGHT;
	int tile_height_stride = tile_height_length * tilemap_width;
	int tile_height_skip = tile_height_stride - tile_height_length;

	int tile_footprint =
		tile_width_length + tile_width_stride * (TILE_HEIGHT - 1);

	// these are in grid units
	int grid_tile_x = tile_index % tilemap_width;
	int grid_tile_y = tile_index / tilemap_width;

	// these are buffer indices
	int tile_top_left =
		(tile_width_length * grid_tile_x) + (tile_height_length * grid_tile_y);
	int tile_bottom_right = tile_top_left + tile_footprint;

	int tile_pixel = 0;

	char *output = PyMem_Malloc(TILE_SIZE);
	ASSERT(tile_bottom_right <= data_len, "Given data is too short.");

	int offset = tile_top_left;
	if (bits_per_pixel == 8) {
		for (int ty = 0; ty < TILE_HEIGHT; ty++) {
			if (ty) offset += tile_width_skip;
			for (int tx = 0; tx < TILE_WIDTH; tx++) {
				output[tile_pixel++] = data[offset++];
			}
		}
	} else {
		for (int ty = 0; ty < TILE_HEIGHT; ty++) {
			if (ty) offset += tile_width_skip;
			for (int tx = 0; tx < (TILE_WIDTH / 2); tx++) {
				output[tile_pixel++] = data[offset] & 0xF;
				output[tile_pixel++] = data[offset] >> 4;
				offset++;
			}
		}
	}
	ASSERT(offset <= tile_bottom_right, "Surpassed bottom right.");
	ASSERT(offset == tile_bottom_right, "Didn't reach bottom right.");

	return PyBytes_FromStringAndSize(output, TILE_SIZE);
}


static void plot_tile(char *dst, int x, int y, char *tile, int width) {
	int k = 0;
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			dst[width * (i + y) + (j + x)] = tile[k++];
		}
	}
}

// pack_ncbr_tiles(tiles : list of bytes, width : int, height : int)
static PyObject *pack_ncbr_tiles(PyObject *self, PyObject *args) {
	PyObject *tiles;
	unsigned int width, height;

	if (!PyArg_ParseTuple(args, "OII", &tiles, &width, &height)) return NULL;

	char *output = PyMem_Malloc(width * height * TILE_SIZE);
	unsigned int x = 0;
	unsigned int y = 0;
	for (unsigned int i = 0; i < width * height; i++) {
		PyObject *tile = PyList_GetItem(tiles, i);
		if (!tile) return NULL;

		char *tiledata;
		Py_ssize_t tilelen;

		ASSERT(
			PyBytes_Check(tile), "List contained something other than bytes");
		PyBytes_AsStringAndSize(tile, &tiledata, &tilelen);
		ASSERT(tilelen == TILE_SIZE, "Tiles should be 64 bytes long");

		plot_tile(output, x, y, tiledata, width * TILE_WIDTH);
		x += TILE_WIDTH;
		if (x >= width * TILE_WIDTH) {
			x = 0;
			y += TILE_WIDTH;
		}
	}

	return PyBytes_FromStringAndSize(output, width * height * TILE_SIZE);
}


// draw_tile_to_buffer(bytearray, tile : bytes, x : int, y : int, buffer_width :
// int)
static PyObject *draw_tile_to_buffer(PyObject *self, PyObject *args) {
	PyObject *bytearray;
	char *buffer, *tile;
	unsigned int width, x, y;
	Py_ssize_t tile_size;

	if (!PyArg_ParseTuple(
			args, "Yy#III", &bytearray, &tile, &tile_size, &x, &y, &width))
		return NULL;
	ASSERT(tile_size == TILE_SIZE, "Tile is not 64 bytes");
	ASSERT(TILE_FITS_ON_BUFFER(x, y, width, PyByteArray_Size(bytearray)),
		"Buffer is too small");
	buffer = PyByteArray_AS_STRING(bytearray);
	plot_tile(buffer, x, y, tile, width);
	Py_RETURN_NONE;
}


static PyMethodDef tileMethods[] = {
	{"_4bpp_to_8bpp", (PyCFunction)_4bpp_to_8bpp, METH_VARARGS,
		"Convert 4bpp bytes to 8bpp"},
	{"_8bpp_to_4bpp", (PyCFunction)_8bpp_to_4bpp, METH_VARARGS,
		"Convert 8bpp bytes to 4bpp"},
	{"flip_tile_data", (PyCFunction)flip_tile_data, METH_VARARGS,
		"Flip a tile"},
	{"read_ncbr_tile", (PyCFunction)read_ncbr_tile, METH_VARARGS,
		"Read a tile from ncbr"},
	{"pack_ncbr_tiles", (PyCFunction)pack_ncbr_tiles, METH_VARARGS,
		"Pack list of bytes into ncbr"},
	{"draw_tile_to_buffer", (PyCFunction)draw_tile_to_buffer, METH_VARARGS,
		"Blit a tile to a bytearray"},
	{NULL, NULL, 0, NULL}};

static struct PyModuleDef tilemodule = {
	PyModuleDef_HEAD_INIT, "tile", "Tile functions.", -1, tileMethods};

PyMODINIT_FUNC PyInit_tile(void) { return PyModule_Create(&tilemodule); }
