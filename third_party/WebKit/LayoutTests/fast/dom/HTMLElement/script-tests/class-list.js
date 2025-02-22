description('Tests the classList attribute and its properties.');

var element;

function createElement(className)
{
    element = document.createElement('p');
    element.className = className;
}

debug('Tests from http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/setting/001.htm
// Firefox throws here but WebKit does not throw on setting readonly idl
// attributes.
createElement('x');
try {
    element.classList = 'y';
    shouldBeEqualToString('String(element.classList)', 'x');
} catch (ex) {
    testPassed('Throwing on set is acceptable');
}

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/001.htm
createElement('');
shouldEvaluateTo('element.classList.length', 0);

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/002.htm
createElement('x');
shouldEvaluateTo('element.classList.length', 1);

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/003.htm
createElement('x x');
shouldEvaluateTo('element.classList.length', 2);

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/004.htm
createElement('x y');
shouldEvaluateTo('element.classList.length', 2);

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/005.htm
createElement('');
element.classList.add('x');
shouldBeEqualToString('element.className', 'x');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/006.htm
createElement('x');
element.classList.add('x');
shouldBeEqualToString('element.className', 'x');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/007.htm
createElement('x  x');
element.classList.add('x');
shouldBeEqualToString('element.className', 'x  x');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/008.htm
createElement('y');
element.classList.add('x');
shouldBeEqualToString('element.className', 'y x');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/009.htm
createElement('');
element.classList.remove('x');
shouldBeEqualToString('element.className', '');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/010.htm
createElement('x');
element.classList.remove('x');
shouldBeEqualToString('element.className', '');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/011.htm
createElement(' y x  y ');
element.classList.remove('x');
shouldBeEqualToString('element.className', ' y y ');

// http://simon.html5.org/test/html/dom/reflecting/DOMTokenList/getting/012.htm
createElement(' x y  x ');
element.classList.remove('x');
shouldBeEqualToString('element.className', 'y');


debug('Ensure that we can handle empty class name correctly');
element = document.createElement('span');
shouldBeTrue("element.classList.toggle('x')");
shouldBeEqualToString('element.className', 'x');
shouldBeFalse("element.classList.toggle('x')");
shouldBeEqualToString('element.className', '');

element = document.createElement('span');
shouldBeFalse('element.classList.contains(\'x\')');
shouldBeUndefined('element.classList[1]');
element.classList.remove('x');
element.classList.add('x')


debug('Test toggle with force argument')

createElement('');
shouldBeTrue("element.classList.toggle('x', true)");
shouldBeEqualToString('element.className', 'x');
shouldBeTrue("element.classList.toggle('x', true)");
shouldBeEqualToString('element.className', 'x');
shouldBeFalse("element.classList.toggle('x', false)");
shouldBeEqualToString('element.className', '');
shouldBeFalse("element.classList.toggle('x', false)");
shouldBeEqualToString('element.className', '');

shouldThrowDOMException(function() {
    element.classList.toggle('', true);
}, DOMException.SYNTAX_ERR);

shouldThrowDOMException(function() {
    element.classList.toggle('x y', false);
}, DOMException.INVALID_CHARACTER_ERR);


debug('Testing add in presence of trailing white spaces.');

createElement('x ');
element.classList.add('y');
shouldBeEqualToString('element.className', 'x y');

createElement('x\t');
element.classList.add('y');
shouldBeEqualToString('element.className', 'x\ty');

createElement(' ');
element.classList.add('y');
shouldBeEqualToString('element.className', ' y');


debug('Test invalid tokens');

// Testing exception due to invalid token

// shouldThrow from js-test.js is not sufficient.
function shouldThrowDOMException(f, ec)
{
    try {
        f();
        testFailed('Expected an exception');
    } catch (ex) {
        if (!(ex instanceof DOMException)) {
            testFailed('Exception is not an instance of DOMException, found: ' +
                       Object.toString.call(ex));
            return;
        }
        if (ec !== ex.code) {
            testFailed('Wrong exception code: ' + ex.code);
            return;
        }
    }
    var formattedFunction = String(f).replace(/^function.+\{\s*/m, '').
        replace(/;?\s+\}/m, '');
    testPassed(formattedFunction + ' threw expected DOMException with code ' + ec);
}

createElement('x');
shouldThrowDOMException(function() {
    element.classList.contains('');
}, DOMException.SYNTAX_ERR);

createElement('x y');
shouldThrowDOMException(function() {
    element.classList.contains('x y');
}, DOMException.INVALID_CHARACTER_ERR);

createElement('');
shouldThrowDOMException(function() {
    element.classList.add('');
}, DOMException.SYNTAX_ERR);

createElement('');
shouldThrowDOMException(function() {
    element.classList.add('x y');
}, DOMException.INVALID_CHARACTER_ERR);

createElement('');
shouldThrowDOMException(function() {
    element.classList.remove('');
}, DOMException.SYNTAX_ERR);

createElement('');
shouldThrowDOMException(function() {
    element.classList.remove('x y');
}, DOMException.INVALID_CHARACTER_ERR);



createElement('');
shouldThrowDOMException(function() {
    element.classList.toggle('');
}, DOMException.SYNTAX_ERR);

createElement('x y');
shouldThrowDOMException(function() {
    element.classList.toggle('x y');
}, DOMException.INVALID_CHARACTER_ERR);

createElement('');
shouldThrow("element.classList.toggle()");

debug('Indexing');

createElement('x');
shouldBeEqualToString('element.classList[0]', 'x');
shouldBeEqualToString('element.classList.item(0)', 'x');

createElement('x x');
shouldBeEqualToString('element.classList[1]', 'x');
shouldBeEqualToString('element.classList.item(1)', 'x');

createElement('x y');
shouldBeEqualToString('element.classList[1]', 'y');
shouldBeEqualToString('element.classList.item(1)', 'y');

createElement('');
shouldBeUndefined('element.classList[0]');
shouldBeNull('element.classList.item(0)');

createElement('x y z');
shouldBeUndefined('element.classList[4]');
shouldBeNull('element.classList.item(4)');
shouldBeUndefined('element.classList[-1]');  // Not a valid index so should not trigger item().
shouldBeNull('element.classList.item(-1)');
shouldThrow('element.classList.item()');

debug('Test case since DOMTokenList is case sensitive');

createElement('x');
shouldBeTrue('element.classList.contains(\'x\')');
shouldBeFalse('element.classList.contains(\'X\')');
shouldBeEqualToString('element.classList[0]', 'x');
shouldThrow('element.classList.contains()');

createElement('X');
shouldBeTrue('element.classList.contains(\'X\')');
shouldBeFalse('element.classList.contains(\'x\')');
shouldBeEqualToString('element.classList[0]', 'X');


debug('Testing whitespace');
// U+0020 SPACE, U+0009 CHARACTER TABULATION (tab), U+000A LINE FEED (LF),
// U+000C FORM FEED (FF), and U+000D CARRIAGE RETURN (CR)

createElement('x\u0020y');
shouldEvaluateTo('element.classList.length', 2);

createElement('x\u0009y');
shouldEvaluateTo('element.classList.length', 2);

createElement('x\u000Ay');
shouldEvaluateTo('element.classList.length', 2);

createElement('x\u000Cy');
shouldEvaluateTo('element.classList.length', 2);

createElement('x\u000Dy');
shouldEvaluateTo('element.classList.length', 2);


debug('DOMTokenList presence and type');


// Safari returns object
// Firefox returns object
// IE8 returns object
// Chrome returns function
// assertEquals('object', typeof DOMTokenList);
shouldBeTrue('\'undefined\' != typeof DOMTokenList');

shouldBeEqualToString('typeof DOMTokenList.prototype', 'object');

createElement('x');
shouldBeEqualToString('typeof element.classList', 'object');

shouldEvaluateTo('element.classList.constructor', 'DOMTokenList');

shouldBeTrue('element.classList === element.classList');

// Bug 93628
document.body.classList.add('FAIL');
shouldBeTrue('document.body.classList.contains("FAIL")');
document.body.classList.remove('FAIL');
shouldBeEqualToString('document.body.className', '');

// Variadic

debug('Variadic calls');

createElement('');
element.classList.add('a', 'b');
shouldBeEqualToString('element.className', 'a b');

element.classList.add('a', 'b', 'c');
shouldBeEqualToString('element.className', 'a b c');

element.classList.add(null, {toString: function() { return 'd' }}, undefined, 0, false);
shouldBeEqualToString('element.className', 'a b c null d undefined 0 false');

createElement('');
element.classList.add('a', 'b', 'a');
shouldBeEqualToString('element.className', 'a b');

createElement('');
shouldThrowDOMException(function() {
    element.classList.add('a', 'b', '');
}, DOMException.SYNTAX_ERR);
shouldBeEqualToString('element.className', '');

shouldThrowDOMException(function() {
    element.classList.add('a', 'b', 'c d');
}, DOMException.INVALID_CHARACTER_ERR);
shouldBeEqualToString('element.className', '');

shouldThrow('element.classList.add("a", {toString: function() { throw new Error("user error"); }}, "b")', '"Error: user error"');
shouldBeEqualToString('element.className', '');

createElement('');
shouldNotThrow('element.classList.add()');

createElement('');
var observer = new WebKitMutationObserver(function() {});
observer.observe(element, {attributes: true});
element.classList.add('a', 'c');
shouldBe('observer.takeRecords().length', '1');


createElement('a b c d  ');
element.classList.remove('a', 'c');
shouldBeEqualToString('element.className', 'b d  ');

element.classList.remove('b', 'b');
shouldBeEqualToString('element.className', 'd  ');

createElement('a b c null d undefined 0 false');
element.classList.remove(null, {toString: function() { return 'd' }}, undefined, 0, false);
shouldBeEqualToString('element.className', 'a b c');

createElement('a b');
shouldThrowDOMException(function() {
    element.classList.remove('a', 'b', '');
}, DOMException.SYNTAX_ERR);
shouldBeEqualToString('element.className', 'a b');

shouldThrowDOMException(function() {
    element.classList.remove('a', 'b', 'c d');
}, DOMException.INVALID_CHARACTER_ERR);
shouldBeEqualToString('element.className', 'a b');

shouldThrow('element.classList.remove("a", {toString: function() { throw new Error("user error"); }}, "b")', '"Error: user error"');
shouldBeEqualToString('element.className', 'a b');

shouldNotThrow('element.classList.remove()');

createElement('a b c');
observer = new WebKitMutationObserver(function() {});
observer.observe(element, {attributes: true});
element.classList.remove('a', 'c');
shouldBe('observer.takeRecords().length', '1');

// iterable<DOMString>;
createElement('a b c');
var seen = [];
for (var t of element.classList) {
    seen.push(t);
}
shouldBeTrue("areArraysEqual(seen, ['a', 'b', 'c'])");
