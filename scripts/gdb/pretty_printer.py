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


class LinkedList:

    def __init__(self, val) -> None:
        self.address = val.dereference().address
        self.next = val.dereference()["next"]
        self.prev = val.dereference()["prev"]

    def to_string_rec(self, prev) -> str:
        string = ''

        if prev:
            string += f" <-"
            if prev.address != self.prev:
                string += f" (ERROR: prev={self.prev}) -"
            string += f"> "

        string += f"{self.address}"
        if self.next:
            string += LinkedList(self.next).to_string_rec(self)

        return string

    def to_string(self) -> str:
        return self.to_string_rec(None)

def pretty_printers(val):
    printers = {
            "avl_t": Avl,
            "llist_t": LinkedList,
    }

    if str(val.type) in printers:
        return printers[str(val.type)](val)

    return None


gdb.pretty_printers.append(pretty_printers)
