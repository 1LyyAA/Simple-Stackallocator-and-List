#include <iostream>
#include <memory>
#include <list>

template<size_t N>
class StackStorage{
    char array[N];
    char* offset{array};
    size_t space{N};

    template<typename T, size_t M>
    friend class StackAllocator;
 
    public:
    StackStorage() = default;
    StackStorage(const StackStorage&) = delete;
    StackStorage& operator=(const StackStorage&) = delete;

};

template<typename T, size_t N>
class StackAllocator{
    public:

    using value_type = T;
    using pointer = T*;
    StackStorage<N>* storage{nullptr};
    
    StackAllocator(StackStorage<N>& stor) : storage(&stor){}

    template <typename U>
    StackAllocator(const StackAllocator<U,N>& other) : storage(other.storage){}

    template <typename U>
    StackAllocator& operator=(const StackAllocator<U,N>& other){
        storage = other.storage;
        return* this;
    }

    T* allocate(size_t n){
        void* ptr = storage->offset;
        ptr = (std::align(alignof(T), n * sizeof(T), ptr, storage->space));
        if(!ptr){
            throw std::bad_alloc();
        }
        //storage->space -= n*sizeof(T);
        storage->offset = static_cast<char*>(ptr) + n * sizeof(T);
        return static_cast<T*>(ptr);
    }

    void deallocate (T*, size_t){}

    StackAllocator select_on_container_copy_construction(){
        return *this;
    }
    template<typename U, size_t M>
    bool operator==(const StackAllocator<U,M>& other)const{
        return storage == other.storage;
    }
    template<typename U, size_t M>
    bool operator!=(const StackAllocator<U,M>& other)const{
        return !(other == *this);
    }

    template <typename U>
    struct rebind{
        using other = StackAllocator<U,N>;
    };

};


template<typename T, typename Alloc = std::allocator<T>>
class List{
    struct BaseNode{
        BaseNode* next;
        BaseNode* prev;

        void swap(BaseNode& other){
            next->prev = &other;
            prev->next = &other;

            other.next->prev = this;
            other.prev->next = this;
            
            BaseNode* tmp = next;
            next = other.next;
            other.next = tmp;

            tmp = prev;
            prev = other.prev;
            other.prev = tmp;

            if(next == &other){
                next = this;
                prev = this;
            }
    
            if(other.next == this){
                other.next = &other;
                other.prev = &other;
            }
        }
    };

    struct Node : BaseNode{
        T value;
    };

    using allocator_type = Alloc;
    using NodeAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using NodeAllocator_traits = std::allocator_traits<NodeAlloc>;

    NodeAlloc alloc_;
    BaseNode fakeNode;
    size_t sz = 0;

    template<typename... Args>
    Node* createNode(Args&&... args){
        Node* node = NodeAllocator_traits::allocate(alloc_, 1);
        try{
            NodeAllocator_traits::construct(alloc_, &(node->value), std::forward<Args>(args)...);
        } catch(...) {
            NodeAllocator_traits::deallocate(alloc_, node, 1);
            throw;
        }
        return node;
    }

    Node* createNode(){
        Node* node = NodeAllocator_traits::allocate(alloc_, 1);
        try{
            NodeAllocator_traits::construct(alloc_, &(node->value));
        } catch(...) {
            NodeAllocator_traits::deallocate(alloc_, node, 1);
            throw;
        }
        return node;
    }
    
    void destroyNode(Node* node){
        NodeAllocator_traits::destroy(alloc_, &(static_cast<Node*>(node)->value));
        NodeAllocator_traits::deallocate(alloc_, node, 1);
    }

    void fork(BaseNode* node_to_fork, BaseNode* node){
        node_to_fork->prev = node->prev;
        node_to_fork->next = node;

        node->prev = node_to_fork;
        node_to_fork->prev->next = node_to_fork;
    }

    void unfork(BaseNode* node){
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    template <bool IsConst>
    class base_iterator{
        friend class List;
        BaseNode* iterator_node;
        
        public:
        using pointer = std::conditional_t<IsConst, const T*,T*>;
        using reference = std::conditional_t<IsConst,const T&, T&>;
        using value_type = T;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type   = std::ptrdiff_t;

        base_iterator(const BaseNode* node) : iterator_node(const_cast<BaseNode*>(node)){}

        operator base_iterator<true>()const noexcept{
            return {iterator_node};
        }

        reference operator*()const noexcept{
            return (static_cast<Node*>(iterator_node)->value);
        }

        pointer operator->()const noexcept{
            return &(static_cast<Node*>(iterator_node)->value);
        }

        base_iterator& operator++(){
            iterator_node = (iterator_node->next);
            return *this;
        }
       
        base_iterator operator++(int){
            iterator_node = (iterator_node->next);
            return {iterator_node->prev};
        }

        base_iterator& operator--(){
            iterator_node = (iterator_node->prev);
            return *this;
        }

        base_iterator operator--(int){
            iterator_node = (iterator_node->prev);
            return {iterator_node->next};
        }
        template<bool U>
        bool operator==(const base_iterator<U>& other)const{
            return (iterator_node == other.iterator_node);
        }
        template<bool U>
        bool operator!=(const base_iterator<U>& other)const{
            return !(*this == other);
        }
    };

    public:
    using iterator = base_iterator<false>;
    using const_iterator = base_iterator<true>;

    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    explicit List(const Alloc& alloc) :  alloc_(alloc), fakeNode{&fakeNode, &fakeNode}{}
    List() : List(Alloc()){}

    explicit List(size_t count, const Alloc& alloc = Alloc()) : List(alloc){
        for(size_t i = 0; i < count; ++i){
            Node* node = createNode();
            fork(node, &fakeNode);
        }
        sz = count;
    }

    explicit List(size_t count, const T& value, const Alloc& alloc = Alloc()) : List(alloc){
        for(size_t i = 0; i < count; ++i){
            Node* node = createNode(value);
            fork(node, &fakeNode);
        }
        sz = count;
    }

    void swap(List& other) {
        fakeNode.swap(other.fakeNode);
        std::swap(sz, other.sz);
        std::swap(alloc_, other.alloc_);
    }

    List(const List& other) : List(std::allocator_traits<Alloc>::select_on_container_copy_construction(other.alloc_)){
        for (auto& x : other){
            push_back(x);
        }
    }

    List& operator=(const List& other){
        if (this == &other) return *this;
        
        if (std::allocator_traits<Alloc>::propagate_on_container_copy_assignment::value)
        {
            List tmp(other.alloc_);
            swap(tmp);
        }

        auto it = begin();
        auto it2 = other.begin();

        for(; it != end() && it2 != other.end(); ++it, ++it2){
            *it = *it2;
        }
            
        for(; it2 != other.end(); ++it2){
            push_back(*it2);
        }

        for(; sz > other.sz;){
            pop_back();
        }
        return *this;
    }

    iterator begin(){
        return{fakeNode.next};
    }

    const_iterator begin()const{
        return{fakeNode.next};
    }

    const_iterator cbegin()const{
        return{fakeNode.next};
    }

    iterator end(){
        return{&fakeNode};
    }

    const_iterator end()const{
        return{&fakeNode};
    }

    const_iterator cend()const{
        return{&fakeNode};
    }

    reverse_iterator rbegin() { 
        return reverse_iterator(end()); 
    }

    reverse_iterator rend() { 
        return reverse_iterator(begin()); 
    }

    const_reverse_iterator rbegin() const { 
        return const_reverse_iterator(end()); 
    }

    const_reverse_iterator rend() const {
        return const_reverse_iterator(begin()); 
    }

    const_reverse_iterator crend() const { 
        return const_reverse_iterator(begin());
    }

    const_reverse_iterator crbegin() const {
        return const_reverse_iterator(begin()); 
    }

    iterator insert (const_iterator pos, const T& value){
        Node* node  = createNode(value);
        fork(node, pos.iterator_node);
        ++sz;
        return {node};
    }

    iterator erase (const_iterator pos){
        BaseNode* node = pos.iterator_node->next;
        unfork(pos.iterator_node);
        destroyNode(static_cast<Node*>(pos.iterator_node));
        --sz;
        return {node};
    }
    

    void push_back(const T& value){
        Node* node = createNode(value);
        fork(node, &fakeNode);
        ++sz;
    }

    void pop_back(){
        Node* node = static_cast<Node*>(fakeNode.prev);
        unfork(node);
        destroyNode(node);
        --sz;
    }

    void push_front(const T& value){
        Node* node = createNode(value);
        fork(node, fakeNode.next);
        ++sz;
    }

    void pop_front(){
        Node* node = static_cast<Node*>(fakeNode.next);
        unfork(node);
        destroyNode(node);
        --sz;
    }

    allocator_type get_allocator()const{
        return this->alloc_;
    }

    size_t size()const{
        return sz;
    }

    bool empty(){
        return (sz==0);
    }

    ~List(){
        while ((&fakeNode != fakeNode.next) && (&fakeNode != fakeNode.prev))
        {
            Node* node = static_cast<Node*>(fakeNode.prev);
            unfork(node);
            destroyNode(node);
        } 
    }
};