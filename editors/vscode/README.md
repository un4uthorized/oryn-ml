# Oryn-ML for Visual Studio Code

Provides syntax highlighting, `.oryn` file association, comment toggling,
bracket matching, automatic bracket closing, basic indentation, completion,
and native API documentation on hover.

Hover over a built-in such as `println`, `Some`, or `array.get()` to see its
signature, parameters, return value, and panic behavior. This documentation is
provided by the language extension; source authors do not need documentation
comments.

## Test locally

Open this directory in VS Code and press `F5`. In the Extension Development
Host window, open any file from the repository's `examples/` directory.

Alternatively, package and install it as a VSIX from the repository root:

```sh
npx --yes @vscode/vsce package editors/vscode \
  --out bin/oryn-ml-0.1.2.vsix
code --install-extension bin/oryn-ml-0.1.2.vsix --force
```

Run `Developer: Reload Window` from the Command Palette after installation. The
status bar should then identify `.oryn` files as `Oryn-ML`.

When native API documentation changes, rebuild and reinstall the VSIX with
`--force`; reloading the window alone does not update the files of an already
installed extension.
