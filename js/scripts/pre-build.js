const fs = require('fs');

function getArgs() {
    const args = {};
    process.argv.slice(2).forEach((arg, index, array) => {
        if (arg.startsWith('-')) {
            args[arg] = array[index + 1];
        }
    });
    return args;
}

const args = getArgs();

const inputFilePath = args['-i'];
const outputFilePath = args['-o'];

if (!inputFilePath || !outputFilePath) {
    console.error('Please provide both input and output file paths using -i and -o flags.');
    process.exit(1);
}

fs.readFile(inputFilePath, 'utf8', (err, data) => {
    if (err) {
        console.error('Error reading the input file:', err);
        return;
    }

    // Replace all occurrences of "args=[]" with "args"
    const modifiedData = data.replace(/args=\[\]/g, 'args');

    fs.writeFile(outputFilePath, modifiedData, 'utf8', (err) => {
        if (err) {
            console.error('Error writing to the output file:', err);
            return;
        }
        console.log('File successfully updated and saved to', outputFilePath);
    });
});