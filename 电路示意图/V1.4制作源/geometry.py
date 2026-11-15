"""Hole-coordinate helpers for the 32 x 18 independent-pad board."""
def pt(s):
    return (int(s[1:]), ord(s[0])-64)

def hole(p):
    return chr(p[1]+64)+str(p[0])

def expand(points):
    """Return every hole traversed by an orthogonal route, in path order."""
    out=[]
    for a,b in zip(points,points[1:]):
        assert a[0]==b[0] or a[1]==b[1],(a,b)
        dx=0 if a[0]==b[0] else (1 if b[0]>a[0] else -1)
        dy=0 if a[1]==b[1] else (1 if b[1]>a[1] else -1)
        p=a
        out.append(p)
        while p!=b:
            p=(p[0]+dx,p[1]+dy)
            out.append(p)
    return list(dict.fromkeys(out))

# The center beneath each DIP8 is reserved for its insulated-lead bypass cap.
blocked={(x,y) for c in (7,17,26) for x in (c+1,c+2) for y in (7,8)}
