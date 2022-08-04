**Optional batch processing version:**

Perform a batch conversion of multiple dicoms using the configurations specified in a yaml file.
```bash
dcm2niibatch batch_config.yml
```

The configuration file should be in yaml format as shown in example `batch_config.yaml`

```yaml
Options:
  isGz:             false
  isFlipY:          false
  isVerbose:        false
  isCreateBIDS:     false
  isOnlySingleFile: false
Files:
    -
      in_dir:           /path/to/first/folder
      out_dir:          /path/to/output/folder
      filename:         dcemri
    -
      in_dir:           /path/to/second/folder
      out_dir:          /path/to/output/folder
      filename:         fa3
```

You can add as many files as you want to convert as long as this structure stays consistent. Note that a dash must separate each file.
