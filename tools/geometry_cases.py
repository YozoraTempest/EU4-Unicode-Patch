"""Declared grapheme fixtures for native editor pixel geometry."""
GEOMETRY_CASES = []
for name, clusters in [
    ('ascii', list('ABCDE')),
    ('chinese', ['A', '中', '文', 'Z']),
    ('supplementary', ['A', '𠀀', '中', 'Z']),
    ('combining', ['A', 'e\u0301', '中', 'Z']),
    ('flag', ['A', '🇨🇳', 'Z']),
    ('family', ['A', '👩‍👩‍👧‍👦', 'Z']),
    ('oversized-supplementary', ['𠀀', 'Z']),
    ('oversized-family', ['👩‍👩‍👧‍👦', 'Z']),
    ('continuation-collision', list('代俣俤俧Z')),
    ('space-combining', ['A', ' \u0301', '中', 'Z']),
    ('empty', []),
]:
    text = ''.join(clusters)
    boundaries = [0]
    for cluster in clusters:
        boundaries.append(boundaries[-1] + len(cluster.encode('utf-8')))
    GEOMETRY_CASES.append(dict(name=name, text=text, boundaries=boundaries))
