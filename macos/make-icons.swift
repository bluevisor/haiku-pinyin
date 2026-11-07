// Draws the menu bar icon, 中 knocked out of a rounded square, as
// a template PDF with the glyphs as paths (no font inside):
//   swiftc macos/make-icons.swift -o build/make-icons && build/make-icons macos/Resources
import CoreGraphics
import CoreText
import Foundation

let directory = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "."
// The width of the system's badges (双, US) in the input menu.
let size = CGSize(width: 22, height: 16)

func glyphPath(_ text: String, size: CGFloat) -> CGPath {
	// The system font draws CJK through a fallback; ask for it by name.
	let font = CTFontCreateWithName("PingFangSC-Semibold" as CFString, size, nil)
	var characters = Array(text.utf16)
	var glyphs = [CGGlyph](repeating: 0, count: characters.count)
	guard CTFontGetGlyphsForCharacters(font, &characters, &glyphs, characters.count),
		let path = CTFontCreatePathForGlyph(font, glyphs[0], nil) else {
		fatalError("no glyph for \(text)")
	}
	return path
}

func icon(_ text: String, _ name: String) {
	var box = CGRect(origin: .zero, size: size)
	let url = URL(fileURLWithPath: directory).appendingPathComponent(name)
	let context = CGContext(url as CFURL, mediaBox: &box, nil)!
	context.beginPDFPage(nil)
	let shape = CGMutablePath()
	shape.addPath(CGPath(roundedRect: box, cornerWidth: 4, cornerHeight: 4,
		transform: nil))
	let glyph = glyphPath(text, size: 12)
	let bounds = glyph.boundingBoxOfPath
	let move = CGAffineTransform(translationX: box.midX - bounds.midX,
		y: box.midY - bounds.midY)
	shape.addPath(glyph, transform: move)
	context.addPath(shape)
	context.setFillColor(CGColor(gray: 0, alpha: 1))
	context.fillPath(using: .evenOdd)
	context.endPDFPage()
	context.closePDF()
}

icon("中", "zh.pdf")
