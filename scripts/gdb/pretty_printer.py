class Avl:

    def __init__(self, val) -> None:
        self.address = val.address
        self.height = val["height"]
        self.parent = val["parent"]
        self.left = val["left"]
        self.right = val["right"]

    def to_string_rec(self, parent, depth) -> str:
        prefix = '  ' * depth
        string = f'''{self.address} {{
  {prefix}height: {self.height}
  {prefix}parent: {self.parent}{"" if self.parent == parent else " (error)"}
'''

        if self.left:
            string += f"  {prefix}left@{Avl(self.left.dereference()).to_string_rec(self.address, depth + 1)}\n"
        if self.right:
            string += f"  {prefix}right@{Avl(self.right.dereference()).to_string_rec(self.address, depth + 1)}\n"

        string += f'{prefix}}}'
        return string

    def to_string(self):
        return f"avl@{self.to_string_rec(0, 0)}"


def pretty_printers(val):
    printers = {
            "avl_t": Avl
    }

    if str(val.type) in printers:
        return printers[str(val.type)](val)

    return None


gdb.pretty_printers.append(pretty_printers)
