const vscode = require("vscode");
const api = require("./language/builtins.json");

function documentation(entry) {
  const markdown = new vscode.MarkdownString();
  markdown.appendCodeblock(entry.signature, "oryn");
  markdown.appendMarkdown(`\n${entry.summary}\n`);
  if (entry.details) markdown.appendMarkdown(`\n${entry.details}\n`);
  if (entry.parameters) {
    markdown.appendMarkdown("\n**Parameters**\n\n");
    for (const [name, description] of Object.entries(entry.parameters)) {
      markdown.appendMarkdown(`- \`${name}\`: ${description}\n`);
    }
  }
  if (entry.returns) markdown.appendMarkdown(`\n**Returns:** ${entry.returns}\n`);
  if (entry.panics) markdown.appendMarkdown(`\n**Panics:** ${entry.panics}\n`);
  if (entry.examples?.length) {
    markdown.appendMarkdown("\n**Examples**\n");
    for (const example of entry.examples) markdown.appendCodeblock(example, "oryn");
  }
  return markdown;
}

function isMethod(document, range) {
  const prefix = document.lineAt(range.start.line).text.slice(0, range.start.character);
  return /\.\s*$/.test(prefix);
}

function completion(name, entry) {
  const kinds = {
    function: vscode.CompletionItemKind.Function,
    method: vscode.CompletionItemKind.Method,
    constructor: vscode.CompletionItemKind.Constructor
  };
  const item = new vscode.CompletionItem(name, kinds[entry.kind]);
  item.detail = entry.signature;
  item.documentation = documentation(entry);
  return item;
}

function activate(context) {
  const selector = { language: "oryn", scheme: "file" };

  context.subscriptions.push(vscode.languages.registerHoverProvider(selector, {
    provideHover(document, position) {
      const range = document.getWordRangeAtPosition(position, /[A-Za-z_][A-Za-z0-9_]*/);
      if (!range) return undefined;
      const name = document.getText(range);
      const entry = (isMethod(document, range) ? api.methods[name] : api.globals[name]) ||
        api.globals[name] || api.methods[name];
      return entry ? new vscode.Hover(documentation(entry), range) : undefined;
    }
  }));

  context.subscriptions.push(vscode.languages.registerCompletionItemProvider(selector, {
    provideCompletionItems(document, position) {
      const prefix = document.lineAt(position.line).text.slice(0, position.character);
      const entries = /\.\s*[A-Za-z_]*$/.test(prefix) ? api.methods : api.globals;
      return Object.entries(entries).map(([name, entry]) => completion(name, entry));
    }
  }, "."));
}

function deactivate() {}

module.exports = { activate, deactivate };
