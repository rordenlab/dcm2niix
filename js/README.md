# @niivue/dcm2niix

`@niivue/dcm2niix` is a JavaScript + WASM library for converting DICOM files to nifti. This library is intended to be **used in the browser**, not in a Node.js environment.

> All operations are performed using the WASM build of [dcm2niix](https://github.com/rordenlab/dcm2niix). The processing takes place in a separate worker thread, so it won't block the main thread in your application.

## Usage

The `@niivue/dcm2niix` JavaScript library offers an object oriented API for working with the `dcm2niix` CLI. Since `dcm2niix` is a CLI tool, the API implemented in `@niivue/dcm2niix` is just a wrapper around the CLI options and arguments. 

## Example

```javascript
// assuming you have an html input element to get directories.
// <input type="file" id="fileInput" webkitdirectory multiple>
import { Dcm2niix } from '@niivue/dcm2niix';

const dcm2niix = new Dcm2niix();
// call the init() method to load the wasm before processing any data
await dcm2niix.init();
// fileInput is the id of the input element with options: webkitdirectory and multiple
fileInput.addEventListener('change', async (event) => {
    inputFileList = event.target.files;
});
// inputFileList is the value from the input element with options: webkitdirectory and multiple
const resultFileList = await dcm2niix.input(inputFileList).run()  
console.log(resultFileList);
// Do something with the resultFileList (normal browser File Objects)
// perhaps view them with @niivue/niivue :) 
```

## Installation

To install `@niivue/dcm2niix` in your project, run the following command:

```bash
npm install @niivue/dcm2niix
```

### To install a local build of the library

Fist, `cd` into the `js` directory of the `dcm2niix` repository.

```bash
# from dcm2niix root directory
cd js
```

To install a local build of the library, run the following command:

```bash
npm run build
```

Then, install the library using the following command:

```bash
npm pack # will create a .tgz file in the root directory
```

Then, install the `@niivue/dcm2niix` library in your application locally using the following command:

```bash
npm install /path/to/niivue-dcm2niix.tgz
```

## Development

First `cd` into the `js` directory of the `dcm2niix` repository.

```bash
# from dcm2niix root directory
cd js
```

To install the dependencies, run the following command:

```bash
npm install
```

To build the library, run the following command

```bash
npm run build
```

To run the tests, run the following command:

```bash
npm run test
```

### Test using a simple demo

To test that the `@niivue/dcm2niix` library is working correctly, you can run the following command:

```bash
npm run demo
```