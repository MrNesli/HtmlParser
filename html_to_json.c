#include <json-c/json_object.h>
#include <json-c/json_util.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <json-c/json.h>

typedef struct Attribute {
    char *name;
    char *value;
} Attribute;

typedef struct HTMLTag {
    char *name;
    char *content;
    struct Attribute **attributes; // array of pointers to attributes
    struct HTMLTag *parent;
    struct HTMLTag **children; // array of pointers to nested tags
    int children_length;
    int attribute_length;
} HTMLTag;

char *valid_tags[] = {
    "body",
    "form",
    "input",
    "p",
    "span",
    "div",
    "a",
    "strong",
    "section",
    "h1", "h2", "h3",
    "h4", "h5", "h6",
    "button",
    "br",
    "img",
    "ul",
    "ol",
    "li",
};

char *non_closing_tags[] = {
    "br",
    "img",
    "input",
};

#define NON_CLOSING_TAGS_LEN (int) (sizeof(non_closing_tags) / sizeof(char*))
#define VALID_TAGS_LEN (int) (sizeof(valid_tags) / sizeof(char*))
#define VALID_ATTR_SPECIAL_CHARS "%?!#$%&'=()*+,-./:;[] "

/* Flags */
bool COMMENT_OPENED = false;

/* Utilities */
size_t strlength(const char *str);
char *remove_chr(char *str, int c);
bool str_in_arr(char *str, char *arr[], int arr_len);
bool char_in(const char *str, int c);
bool strequals(const char *str1, const char *str2);
bool is_valid_tag(HTMLTag *tag);
bool is_opening_tag(HTMLTag *tag);
bool is_closing_tag(HTMLTag *tag);
bool open_close_tags_match(char *open_tag, char *close_tag);
void print_all_tags(HTMLTag *root, int padding);
char *get_tag_type(HTMLTag *tag);

/* I/O */
FILE *open_file();
char *readline(FILE *fp);

/* HTMLTag/Attribute */
Attribute *create_attribute(const char *name, const char *value);
HTMLTag *create_tag_from_string(const char *name, const char *content);
HTMLTag *next_tag(char **line_ptr);

/* Adding HTMLTags/Attributes */
void add_child(HTMLTag *parent, HTMLTag *child);
void add_attribute(HTMLTag *tag, Attribute *attr);
HTMLTag *parse_tags(FILE *stream);

/* JSON */
json_object *json_create_attributes_array(Attribute **attrs, int attrs_length);
json_object *json_create_tag(HTMLTag *tag);
void json_traverse_children_and_create_tags(HTMLTag *root, json_object *json_root, json_object *root_children);

/*
 * Returns the length of a given string
 */
size_t strlength(const char *str) {
    const char *s = str;
    while (*s != '\0') s++;
    return s - str;
}

/*
 * Returns true if two strings are equal
 */
bool strequals(const char *str1, const char *str2) {
    return strcmp(str1, str2) == 0;
}

/*
 * Returns true if a given tag is an opening one
 * E.g. <p> <span>
 */
bool is_opening_tag(HTMLTag *tag) {
    return strcmp(get_tag_type(tag), "opening") == 0;
}

/*
 * Returns true if a given tag is a closing one
 * E.g. </p> </span>
 */
bool is_closing_tag(HTMLTag *tag) {
    return strcmp(get_tag_type(tag), "closing") == 0;
}

/*
 * Returns true if a given tag is a non-closing one
 * E.g. <img> <br>
 */
bool is_non_closing_tag(HTMLTag *tag) {
    return strcmp(get_tag_type(tag), "non_closing") == 0;
}

/*
 * Returns true if given tag is valid otherwise false
 * Example of valid tags: <span> <p> </a> </section>
 * Example of invalid tags: <spon> <section/> <stro/ng>
 */
bool is_valid_tag(HTMLTag *tag) {
    bool needs_freeing = false;
    bool is_valid = false;
    char *tmp_tag = NULL;
    int valid_tags_length = sizeof(valid_tags) / sizeof(char*);

    // If it's a closing tag we remove the slash
    if (tag->name[0] == '/') {
        // Newly allocated string
        tmp_tag = remove_chr(tag->name, '/');
        needs_freeing = true;
    }
    else {
        tmp_tag = tag->name;
    }

    if (str_in_arr(tmp_tag, valid_tags, VALID_TAGS_LEN)) {
        is_valid = true;
    }

    if (needs_freeing) 
        free(tmp_tag);

    return is_valid;
}

/*
 * Returns true if open and close tags match otherwise false
 * E.g. <span> and </span> match
 *      <p> and <a> do not match
 */
bool open_close_tags_match(char *open_tag, char *close_tag) {
    bool match = false;
    char *new_close_tag = remove_chr(close_tag, '/');

    if (strcmp(open_tag, new_close_tag) == 0)
        match = true;

    free(new_close_tag);
    return match;
}

/*
 * Checks whether a given character is present in str or not
 */
bool char_in(const char *str, int c) {
    for (int i = 0; i < strlength(str); i++) {
        if (str[i] == c) 
            return true;
    }

    return false;
}

/*
 * Returns a newly allocated copy of str with a given character removed
 * E.g. remove_chr("testing", 'e') => "tsting"
 */
char *remove_chr(char *str, int c) {
    char *chr_pointer = strchr(str, c);

    if (chr_pointer == NULL) {
        printf("No character %c found.\n", (char) c);
        return NULL;
    }

    int str_length = strlength(str);
    int substr_length = strlength(chr_pointer);

    char *result = (char*) calloc(str_length, sizeof(char));
    int offset = 0;

    // Left part of the string
    for (int i = 0; i < (str_length - substr_length); i++) {
        result[offset++] = str[i];
    }

    // Right part of the string
    for (int i = (str_length - substr_length + 1); i < str_length; i++) {
        result[offset++] = str[i];
    }

    return result;
}

/*
 * Returns true if a given string is present in array, otherwise false
 */
bool str_in_arr(char *str, char *arr[], int arr_len) {
    for (int i = 0; i < arr_len; i++) {
        if (strequals(str, *(arr + i)))
            return true;
    }

    return false;
}

/*
 * Makes sure the tag is valid and returns whether it's a closing or an opening one 
 * E.g. <span> => "opening"
 *      </p> => "closing"
 *      <sp/oon> => Invalid tag error
 */
char *get_tag_type(HTMLTag *tag) {
    if (is_valid_tag(tag)) {
        if (str_in_arr(tag->name, non_closing_tags, NON_CLOSING_TAGS_LEN)) {
            return "non_closing";
        }
        if (tag->name[0] == '/') {
            return "closing";
        }
        else {
            return "opening";
        }
    }
    else {
        printf("Got invalid tag: %s\n", tag->name);
        exit(1);
    }
}

/*
 * Prints all parsed tags with padding
 */
void print_all_tags(HTMLTag *root, int padding) {
    printf("<%s>\n", root->name);

    for (int i = 0; i < root->children_length; i++) {
        for (int j = 0; j < padding; j++) {
            putchar(' ');
        }

        HTMLTag *child = *(root->children + i);

        if (child->children_length > 0) {
            print_all_tags(child, padding + 2);
        }
        else {
            printf("<%s>\n", child->name);
        }
    }
}

/*
 * Frees memory of a given HTMLTag and all its children
 */
void free_tag(HTMLTag *root) {
    printf("Freeing <%s>\n", root->name);

    // Free children tags
    for (int i = 0; i < root->children_length; i++) {
        HTMLTag *child = *(root->children + i);
        free_tag(child);
    }

    // Free attributes
    for (int i = 0; i < root->attribute_length; i++) {
        Attribute *attr = *(root->attributes + i);
        free(attr->name);
        free(attr->value);
        free(attr);
    }

    // Free the pointer to attributes
    if (root->attributes != NULL)
        free(root->attributes);

    // Free tag name
    if (root->name != NULL)
        free(root->name);

    // Free tag content
    if (root->content != NULL)
        free(root->content);

    // Free the pointer to children tags
    if (root->children != NULL)
        free(root->children);

    // Free the pointer to HTMLTag struct
    free(root);
}

/* 
 * Opens a file stream and returns a pointer to it
 */
FILE *open_file() {
    FILE *fptr;
    const char *fname = "index.html";

    fptr = fopen(fname, "r");
    if (!fptr) {
        perror("File opening failed");
        return NULL;
    }

    return fptr;
}

/* 
 * Reads a line from file stream
 */
char *readline(FILE *fp) {
    int offset = 0;
    int bufsize = 4;
    char *buf = (char*) calloc(bufsize, sizeof(char));
    int c;

    if (buf == NULL) {
        perror("Failed to allocate memory for the buffer");
        return NULL;
    }

    // Right-most expression in while-loop must be the condition
    while(c = fgetc(fp), c != EOF) {
        if (offset == bufsize - 1) { // -1 for the NULL terminator
            bufsize *= 2;

            char *new_buf = realloc(buf, bufsize);
            if (new_buf == NULL) {
                perror("Failed to reallocate memory for the buffer");
                return NULL;
            }

            buf = new_buf;
        }

        // Including new line char in the read string
        // Adding this after offset/bufsize check because the last byte
        // might not get allocated
        if (c == '\n') {
            buf[offset++] = c;
            break;
        }

        buf[offset++] = c;
    }

    // Adjusting buffer size to fit the string
    if (offset < bufsize - 1) {
        // Zero characters read
        if (offset == 0) {
            free(buf);
            return NULL;
        }

        char *new_buf = realloc(buf, offset + 1);

        if (new_buf == NULL) {
            perror("Failed to adjust allocated memory for the buffer");
            return NULL;
        }

        buf = new_buf;
    }

    buf[offset] = '\0';

    return buf;
}


/* 
 * Allocates memory for an Attribute struct, initializes its fields, and returns a pointer to it 
 */
Attribute *create_attribute(const char *name, const char *value) {
    Attribute *attr = calloc(1, sizeof(Attribute));
    attr->name = strdup(name);
    attr->value = strdup(value);
    return attr;
}

/* 
 * Allocates memory for a HTMLTag struct, initializes its fields, and returns a pointer to it 
 */
HTMLTag *create_tag_from_string(const char *name, const char *content) {
    HTMLTag *tag = (HTMLTag*) calloc(1, sizeof(HTMLTag));
    
    if (name == NULL) {
        printf("Cannot create HTML tag without a name\n");
        exit(1);
    }

    // Copy string name to tag's name
    tag->name = strdup(name);
    if (tag->name == NULL) {
        printf("Failed to copy string '%s' to tag name\n", name);
        exit(1);
    }

    if (content != NULL) {
        // Copy string content to tag's content
        tag->content = strdup(content);
        if (tag->content == NULL) {
            printf("Failed to copy string '%s' to tag content\n", content);
            exit(1);
        }
    }

    return tag;
}

/* 
 * Dynamically adds child tag to the parent tag 
 */
void add_child(HTMLTag *parent, HTMLTag *child) {
    if (parent == NULL || child == NULL) return;

    if (parent->children_length == 0) {
        parent->children_length++;
        parent->children = (HTMLTag**) malloc(sizeof(HTMLTag*));

        if (parent->children == NULL) {
            free(parent->children);
            printf("Failed to allocate memory for children HTMLTag\n");
            exit(1);
        }

        *(parent->children + parent->children_length - 1) = child;
    }
    else {
        parent->children_length++;
        HTMLTag **new_children_ptr = (HTMLTag**) realloc(parent->children, sizeof(HTMLTag*) * parent->children_length);

        if (new_children_ptr == NULL) {
            free(new_children_ptr);
            printf("Failed to reallocate memory for children HTMLTags\n");
            exit(1);
        }

        parent->children = new_children_ptr;
        *(parent->children + parent->children_length - 1) = child;
    }
}

/* 
 * Dynamically adds an attribute to the tag 
 */
void add_attribute(HTMLTag *tag, Attribute *attr) {
    if (tag == NULL || attr == NULL) return;

    if (tag->attribute_length == 0) {
        tag->attributes = (Attribute**) malloc(sizeof(Attribute*));

        if (tag->attributes == NULL) {
            free(tag->attributes);
            perror("Failed to allocate memory for Attributes");
            exit(1);
        }

        *(tag->attributes + tag->attribute_length) = attr;
        tag->attribute_length++;
    }
    else {
        tag->attribute_length++;
        Attribute **new_attr_ptr = (Attribute**) realloc(tag->attributes, sizeof(Attribute*) * tag->attribute_length);

        if (new_attr_ptr == NULL) {
            free(new_attr_ptr);
            perror("Failed to reallocate memory for Attributes");
            exit(1);
        }

        tag->attributes = new_attr_ptr;
        *(tag->attributes + tag->attribute_length - 1) = attr;
    }
}

/* 
 * Searches for the next tag in a line pointed to by line_ptr 
 * Once found, it shifts the line pointer to the next character after the closing arrow of found tag 
 *
 * E.g. next_tag(&"<span><a></a></span>") returns tag; line_ptr = &"<a></a></span>"
 *      next_tag(&"<a></a></span>") returns tag; line_ptr = &"</a></span>"
 *      ...
 */
HTMLTag *next_tag(char **line_ptr) {
    char *expected_token = "open_tag";

    Attribute *attr = NULL;
    char *attr_name = (char*) calloc(128, sizeof(char));
    char *attr_value = (char*) calloc(128, sizeof(char));

    HTMLTag *tag = NULL;
    char *tag_name = (char*) calloc(128, sizeof(char));
    char *tag_content = (char*) calloc(1024, sizeof(char));

    char *line = *line_ptr; // To be able to mutate the pointer to the line we need to modify a pointer to the pointer to the line
    bool error_exit = false;
    int offset = 0;

    // Character at which is pointing the line pointer
    int chr;

    printf("Current line: %s\n", line);
    printf("Expected: %s\n", expected_token);

    // TODO: Maybe replace this implementation with regexes
    while ((chr = *line), chr != EOF && chr != '\n') {
        // We parse tags character by character following the chain of expected tokens:
        //   
        //   
        //             attr_value_open     attr_separator_or_close_tag
        //     open_tag    |                         |
        //       |         |                         |\ 
        //       v         v\                        vv
        //       <div class="text-red-400 text-center">
        //        ^  ^^                ^
        //        |  |/                 |
        //        |  |                  |
        //        | attr_name           |
        //        |                     |
        //      tag_name                |
        //                          attr_value
        //   
        //
        //
        
        if (COMMENT_OPENED) {
            // -->\n
            if (chr == '-') {
                if (strlength(line) >= 3) {
                    if (*(line + 1) == '-' && *(line + 2) == '>') {
                        printf("Comment closed\n");
                        COMMENT_OPENED = false;
                        expected_token = "open_tag";
                        line += 3;
                        continue;
                    }
                }
            }
        }
        else if (strequals(expected_token, "open_tag")) {
            if (chr == '<') {
                expected_token = "tag_name";
                offset = 0;
                printf("Expected: %s\n", expected_token);
            }
            // We skip spaces if there is no content
            else if (chr == ' ' && strlength(tag_content) == 0){
                line++;
                continue;
            }
            else {
                tag_content[offset++] = chr;
            }
        }
        else if (strequals(expected_token, "tag_name")) {
            if (isalnum(chr) || chr == '/') {
                tag_name[offset++] = chr;
            }
            else if (chr == '>' && strlength(tag_name) > 0) {
                // If the tag is not of closing type, it can't have content
                if (tag_name[0] != '/') {
                    free(tag_content);
                    tag_content = NULL;
                } 

                tag = create_tag_from_string(tag_name, tag_content);
                line++;
                printf("Closing arrow\n");
                break;
            }
            else if (chr == ' ' && strlength(tag_name) > 0) {
                // TODO: In here we know for sure it's an opening tag.
                // If tag_content isn't empty, means that it's arbitrary text
                // that belongs to this tag's parent. Shadow text tag
                tag = create_tag_from_string(tag_name, NULL);
                expected_token = "attr_name";
                offset = 0;

                printf("Expected: %s\n", expected_token);
            }
            // Comment tag
            else if (chr == '!') {
                // !--
                printf("Comment opened\n");
                if (strlength(line) >= 3) {
                    if (*(line + 1) == '-' && *(line + 2) == '-') {
                        COMMENT_OPENED = true;
                        line += 3;
                        continue;
                    }
                    else
                        error_exit = true;
                }
                else
                    error_exit = true;

                if (error_exit) {
                    printf("Invalid comment syntax.\n");
                    break;
                }
            }
            else {
                error_exit = true;
                printf("Expected %s: Bad tag\n", expected_token);
                break;
            }
        }
        else if (strequals(expected_token, "attr_name")) {
            if (isalpha(chr)) {
                attr_name[offset++] = chr;
            }
            // Attribute value separator
            else if (chr == '=' && strlength(attr_name) > 0) {
                expected_token = "attr_value_open";
                offset = 0;

                printf("Expected: %s\n", expected_token);
            }
            else {
                error_exit = true;
                printf("Expected %s: Bad tag\n", expected_token);
                break;
            }
        }
        else if (strequals(expected_token, "attr_value_open")) {
            if (chr == '"') {
                expected_token = "attr_value";
                printf("Expected: %s\n", expected_token);
            }
            else {
                error_exit = true;
                printf("Expected %s: Bad tag. No opening quotes in attribute value\n", expected_token);
                break;
            }
        }
        else if (strequals(expected_token, "attr_value")) {
            if (isalnum(chr) || char_in(VALID_ATTR_SPECIAL_CHARS, chr)) {
                attr_value[offset++] = chr;
            }
            else if (chr == '"') {
                expected_token = "attr_separator_or_close_tag";
                offset = 0;

                // Add attr to HTMLTag
                attr = create_attribute(attr_name, attr_value);
                add_attribute(tag, attr);

                printf("Expected: %s\n", expected_token);

                free(attr_name);
                free(attr_value);

                // Reset pointers to null so that we know we don't need to free them anymore
                // We don't free attr because it's a pointer that's now attached to the tag
                attr = NULL;
                attr_name = NULL;
                attr_value = NULL;
            }
            else {
                error_exit = true;
                printf("Expected %s: Bad tag.\n", expected_token);
                break;
            }
        }
        else if (strequals(expected_token, "attr_separator_or_close_tag")) {
            if (chr == ' ') {
                expected_token = "attr_name";

                // Reallocate freed pointers
                attr_name = (char*) calloc(128, sizeof(char));
                attr_value = (char*) calloc(128, sizeof(char));

                printf("Expected: %s\n", expected_token);
            }
            else if (chr == '>') {
                printf("Closing arrow\n");
                line++;
                break;
            }
            else {
                error_exit = true;
                printf("Expected %s: Bad tag.\n", expected_token);
                break;
            }
        }

        // Advance the line pointer
        line++;
    }

    // Free all allocated pointers
    if (attr_name) free(attr_name);
    if (attr_value) free(attr_value);
    if (tag_name) free(tag_name);
    if (tag_content) free(tag_content);

    if (error_exit) {
        if (tag) {
            free_tag(tag);
        } 
        exit(1);
    }

    *line_ptr = line;

    return tag;
}

HTMLTag *parse_tags(FILE *stream) {
    // Here's the idea:
    // Find opening tag, set it as current_tag
    // If another opening tag is found, set it as current_tag and parent is previous_tag
    // If a closing tag is found and it matches current_tag, set current_tag as a child of its parent, and set current_tag to parent 
    // If a closing tag is found and there's no parent, we reached the root tag
    // If a closing tag is found and it doesn't match the current_tag, throw an error "Invalid syntax"
    // Continue parsing
    
    char *line = NULL;
    HTMLTag *current_tag = NULL;

    while((line = readline(stream)) != NULL) {
        // We store the pointer to the whole line to be able to free it at the end
        char *init_line_ptr = line;

        while (*line) {
            // TODO: Create a function that will free current_tag and all its children
            HTMLTag *tag = next_tag(&line);

            if (tag == NULL) {
                break;
            }
            // Root opening tag
            else if (!current_tag && is_opening_tag(tag)) {
                printf("Found opening tag\n");
                current_tag = tag;
            }
            // Closing tag without opening
            else if (!current_tag && is_closing_tag(tag)) {
                printf("Found tag: %s\n", tag->name);
                perror("Closing tag must be preceeded with opening one");
                exit(1);
            }
            // Nested opening tag
            else if (current_tag && is_opening_tag(tag)) {
                printf("Found opening tag\n");
                tag->parent = current_tag;
                current_tag = tag;
            }
            // Non-closing tag
            else if (current_tag && is_non_closing_tag(tag)) {
                printf("Found non-closing tag\n");
                add_child(current_tag, tag);
            }
            // Closing tag
            else if (current_tag && is_closing_tag(tag)) {
                printf("Found closing tag\n");
                printf("Current tag name: %s\n", current_tag->name);

                if (open_close_tags_match(current_tag->name, tag->name)) {
                    printf("Current opening tag and found closing tag match\n");
                    if (current_tag->parent != NULL) {
                        // Allocating memory for the tag pair's content  
                        if (tag->content != NULL) {
                            current_tag->content = strdup(tag->content); // +1 for null terminator

                            if (!current_tag->content) {
                                printf("Failed to allocate memory for tag's content\n");
                                exit(1);
                            }
                        }

                        // We aren't using the closing tag anywhere, so we free it
                        free_tag(tag);

                        // Adding the tag pair to the parent tag 
                        HTMLTag *parent = current_tag->parent;
                        add_child(parent, current_tag);
                        current_tag = parent;
                    }
                    else {
                        free_tag(tag);
                    }
                }
                else {
                    printf("Opening and closing tags do not match.");
                    exit(1);
                }
            }
            else {
                free_tag(tag);
            }
        }

        free(init_line_ptr);
    }

    // Preview the HTML tags tree
    printf("\n\n\nHTML Preview:\n");
    print_all_tags(current_tag, 2);
    printf("\n\n");

    return current_tag;
}

/*
 * Creates an array of attribute objects and returns the pointer to the json object
 */
json_object *json_create_attributes_array(Attribute **attrs, int attrs_length) {
    json_object *json_attrs = json_object_new_array();

    for (int i = 0; i < attrs_length; i++) {
        Attribute *attr = *(attrs + i);

        json_object *json_attr = json_object_new_object();
        json_object_object_add(json_attr, "name", json_object_new_string(attr->name));
        json_object_object_add(json_attr, "value", json_object_new_string(attr->value));

        json_object_array_add(json_attrs, json_attr);
    }

    return json_attrs;
}

/*
 * Creates a HTMLTag json object and returns the pointer to it
 */
json_object *json_create_tag(HTMLTag *tag) {
    json_object *json_tag = json_object_new_object();
    json_object_object_add(json_tag, "name", json_object_new_string(tag->name));

    if (tag->content) 
        json_object_object_add(json_tag, "content", json_object_new_string(tag->content));

    json_object_object_add(json_tag, "children_length", json_object_new_int(tag->children_length));

    if (tag->attribute_length > 0)
        json_object_object_add(json_tag, "attributes", json_create_attributes_array(tag->attributes, tag->attribute_length));

    json_object_object_add(json_tag, "attribute_length", json_object_new_int(tag->attribute_length));

    return json_tag;
}

/*
 * Recursively traverses root HTMLTag and its children creating json arrays containing respective nested HTMLTags
 */
void json_traverse_children_and_create_tags(HTMLTag *root, json_object *json_root, json_object *root_children) {
    bool add_children_array = root->children_length > 0;

    for (int i = 0; i < root->children_length; i++) {
        HTMLTag *child = *(root->children + i);
        json_object *json_child = json_create_tag(child);
        json_object *child_children = json_object_new_array();

        json_object_array_add(root_children, json_child);
        if (child->children_length > 0) {
            json_traverse_children_and_create_tags(child, json_child, child_children);
        }
        else {
            json_object_put(child_children);
        }
    }

    if (add_children_array)
        json_object_object_add(json_root, "children", root_children);
}

int main(void) {
    FILE *stream = open_file();
    char *json_filename = "index.json";

    if (!stream) return 1;

    // Root HTML tag
    HTMLTag *root_tag = parse_tags(stream);

    // Array of tags
    json_object *tags = json_object_new_array();

    json_object *json_root_tag = json_create_tag(root_tag);
    json_object *json_root_tag_children = json_object_new_array();

    json_traverse_children_and_create_tags(root_tag, json_root_tag, json_root_tag_children);

    json_object_array_add(tags, json_root_tag);

    // Save JSON to file
    if (json_object_to_file_ext(json_filename, tags, JSON_C_TO_STRING_PRETTY)) {
        printf("Failed to save JSON to %s\n", json_filename);
    }
    else {
        printf("Saved JSON representation to %s\n", json_filename);
    }

    // Free and cleanup everything
    json_object_put(tags);
    free_tag(root_tag);
    fclose(stream);

    return 0;
}
