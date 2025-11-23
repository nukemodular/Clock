#!/usr/bin/env python3
"""
Recolour and extract dancer SVG layers into independent SVG files for embedding.

Usage:
  python tools/dancer_recolor_extract.py --input dancer/dancer_all.svg --out-dir dancer/frames --accent FF4E5B

Outputs:
  frames/layer_<index>.svg  (each original <g id="Layer*"> group wrapped in standalone svg)
  frames/dancer_all_recoloured.svg (full recoloured master)

Then run JUCE BinaryBuilder to embed:
  <path-to-BinaryBuilder> frames/dancer_all_recoloured.svg frames/layer_*.svg -I Source -o Source/DancerBinaryData.cpp
"""
import argparse, os, re, sys
import xml.etree.ElementTree as ET

def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument('--input', required=True)
    ap.add_argument('--out-dir', required=True)
    ap.add_argument('--accent', default='FF4E5B', help='Hex RGB without #')
    return ap.parse_args()

def scrub_element(el, accent):
    # Remove style attribute; set fill/stroke recursively
    if 'style' in el.attrib:
        del el.attrib['style']
    if el.tag.endswith('g') or el.tag.endswith('path') or el.tag.endswith('rect') or el.tag.endswith('circle') or el.tag.endswith('ellipse') or el.tag.endswith('polygon') or el.tag.endswith('polyline'):
        el.set('fill', f'#{accent}')
        el.set('stroke', f'#{accent}')
    for child in list(el):
        scrub_element(child, accent)

def main():
    args = parse_args()
    accent = args.accent.lstrip('#')
    tree = ET.parse(args.input)
    root = tree.getroot()
    os.makedirs(args.out_dir, exist_ok=True)

    # Recolour full document
    scrub_element(root, accent)
    full_out = os.path.join(args.out_dir, 'dancer_all_recoloured.svg')
    ET.indent(tree, space='  ')
    tree.write(full_out, encoding='utf-8', xml_declaration=True)
    print(f'[OK] Wrote recoloured master: {full_out}')

    # Extract layers
    layers = []
    for child in list(root):
        if child.tag.endswith('g'):
            layer_id = child.attrib.get('id','')
            if re.match(r'(?i)layer\d+', layer_id):
                layers.append(child)

    if not layers:
        print('[WARN] No Layer* groups found.')
        return

    viewBox = root.attrib.get('viewBox','0 0 355 500')
    width   = root.attrib.get('width','355')
    height  = root.attrib.get('height','500')

    for i, layer in enumerate(layers):
        svg = ET.Element('svg', {
            'xmlns':'http://www.w3.org/2000/svg',
            'viewBox': viewBox,
            'width': width,
            'height': height
        })
        # Deep copy layer
        svg.append(ET.fromstring(ET.tostring(layer, encoding='utf-8')))
        out_path = os.path.join(args.out_dir, f'layer_{i:02d}.svg')
        layer_tree = ET.ElementTree(svg)
        ET.indent(layer_tree, space='  ')
        layer_tree.write(out_path, encoding='utf-8', xml_declaration=True)
        print(f'[OK] Wrote {out_path}')

if __name__ == '__main__':
    main()
