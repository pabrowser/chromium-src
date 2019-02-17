// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(async function() {
  TestRunner.addResult(`Tests that source map is not detached on edits. crbug.com/257778\n`);
  await TestRunner.loadModule('sources_test_runner');
  await TestRunner.loadModule('elements_test_runner');
  await TestRunner.showPanel('sources');
  await TestRunner.showPanel('elements');
  await TestRunner.loadHTML(`
      <div id="container">container.</div>
    `);
  await TestRunner.addStylesheetTag('./resources/styles-do-not-detach-sourcemap-on-edits.css');

  SourcesTestRunner.waitForScriptSource('styles-do-not-detach-sourcemap-on-edits.scss', onSourceMapLoaded);

  function onSourceMapLoaded() {
    ElementsTestRunner.selectNodeAndWaitForStyles('container', onNodeSelected);
  }

  function onNodeSelected() {
    TestRunner.runTestSuite(testSuite);
  }

  var testSuite = [
    function editProperty(next) {
      ElementsTestRunner.dumpSelectedElementStyles(true, false, true);

      var treeItem = ElementsTestRunner.getMatchedStylePropertyTreeItem('color');
      treeItem.applyStyleText('NAME: VALUE', true);
      ElementsTestRunner.waitForStyles('container', next);
    },

    function editSelector(next) {
      ElementsTestRunner.dumpSelectedElementStyles(true, false, true);

      var section = ElementsTestRunner.firstMatchedStyleSection();
      section.startEditingSelector();
      section._selectorElement.textContent = '#container, SELECTOR';
      ElementsTestRunner.waitForSelectorCommitted(next);
      section._selectorElement.dispatchEvent(TestRunner.createKeyEvent('Enter'));
    },

    function editMedia(next) {
      ElementsTestRunner.dumpSelectedElementStyles(true, false, true);

      var section = ElementsTestRunner.firstMatchedStyleSection();
      var mediaTextElement = ElementsTestRunner.firstMediaTextElementInSection(section);
      mediaTextElement.click();
      mediaTextElement.textContent = '(max-width: 9999999px)';
      ElementsTestRunner.waitForMediaTextCommitted(next);
      mediaTextElement.dispatchEvent(TestRunner.createKeyEvent('Enter'));
    },

    function addRule(next) {
      ElementsTestRunner.dumpSelectedElementStyles(true, false, true);

      var styleSheetHeader = TestRunner.cssModel.styleSheetHeaders().find(
          header => header.resourceURL().indexOf('styles-do-not-detach-sourcemap-on-edits.css') !== -1);
      if (!styleSheetHeader) {
        TestRunner.addResult('ERROR: failed to find style sheet!');
        TestRunner.completeTest();
        return;
      }
      ElementsTestRunner.addNewRuleInStyleSheet(styleSheetHeader, 'NEW-RULE', next);
    },

    function finish(next) {
      ElementsTestRunner.dumpSelectedElementStyles(true, false, true);
      next();
    },
  ];
})();