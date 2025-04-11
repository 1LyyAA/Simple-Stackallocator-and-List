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

    // внутренняя реализация листа
    class list_impl : public NodeAlloc{
        BaseNode fakeNode;
        size_t sz = 0;
        friend class List;

        template<typename... Args>
        Node* createNode(Args&&... args){
            Node* node = NodeAllocator_traits::allocate((*this), 1);
            try{
                NodeAllocator_traits::construct((*this), &(node->value), std::forward<Args>(args)...);
            } catch(...) {
                NodeAllocator_traits::deallocate((*this), node, 1);
                throw;
            }
            return node;
        }

        void destroyNode(Node* node){
            NodeAllocator_traits::destroy((*this), &(static_cast<Node*>(node)->value));
            NodeAllocator_traits::deallocate((*this), node, 1);
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

        explicit list_impl(const Alloc& alloc) :  NodeAlloc(alloc) {
            fakeNode = {&fakeNode, &fakeNode};
        }
        list_impl() : list_impl(Alloc()){}

        explicit list_impl(size_t count, const Alloc& alloc = Alloc()) : list_impl(alloc){
            for(size_t i = 0; i < count; ++i){
                Node* node = createNode();
                fork(node, &fakeNode);
            }
            sz = count;
        }

        explicit list_impl(size_t count, const T& value, const Alloc& alloc = Alloc()) : list_impl(alloc){
            for(size_t i = 0; i < count; ++i){
                Node* node = createNode(value);
                fork(node, &fakeNode);
            }
            sz = count;
        }

        void swap(list_impl& other) {
            fakeNode.swap(other.fakeNode);
            std::swap(sz, other.sz);
            std::swap(static_cast<NodeAlloc&>(*this), static_cast<NodeAlloc&>(other));
        }

        ~list_impl(){
            while ((&fakeNode != fakeNode.next) && (&fakeNode != fakeNode.prev))
            {
                Node* node = static_cast<Node*>(fakeNode.prev);
                unfork(node);
                destroyNode(node);
            } 
        }
    };

    list_impl list;

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

    iterator begin(){
        return{list.fakeNode.next};
    }

    const_iterator begin()const{
        return{list.fakeNode.next};
    }

    const_iterator cbegin()const{
        return{list.fakeNode.next};
    }

    iterator end(){
        return{&list.fakeNode};
    }

    const_iterator end()const{
        return{&list.fakeNode};
    }

    const_iterator cend()const{
        return{&list.fakeNode};
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

    public:
    explicit List(const Alloc& alloc) : list(alloc){}
    List() : List(Alloc()){}

    explicit List(size_t count, const Alloc& alloc = Alloc()) : list(count, alloc){}

    explicit List(size_t count, const T& value, const Alloc& alloc = Alloc()) : list(count, value, alloc){}

    List(const List& other) : list(std::allocator_traits<Alloc>::select_on_container_copy_construction(static_cast<const NodeAlloc&>(other.list))){
        for (auto& x : other){
            push_back(x);
        }
    }

    List(List&& other) : list(other.get_allocator()){
        list.swap(other.list);
    }

    List& operator=(const List& other){
        if (this == &other) return *this;
        
        if (std::allocator_traits<NodeAlloc>::propagate_on_container_copy_assignment::value)
        {
            List tmp(other.get_allocator());
            list.swap(tmp.list);
        }

        auto it = begin();
        auto it2 = other.begin();

        for(; it != end() && it2 != other.end(); ++it, ++it2){
            *it = *it2;
        }
            
        for(; it2 != other.end(); ++it2){
            push_back(*it2);
        }

        for(; list.sz > other.list.sz;){
            pop_back();
        }
        return *this;
    }

    List& operator=(List&& other){
        if(*this == other) return *this;
        List tmp(std::move(other));
        list.swap(tmp.list);
        return *this;
    }

    template <typename U>
    void push_back(U&& value){
        Node* node = list.createNode(std::forward<U>(value));
        list.fork(node, &list.fakeNode);
        ++list.sz;
    }

    void pop_back(){
        Node* node = static_cast<Node*>(list.fakeNode.prev);
        list.unfork(node);
        list.destroyNode(node);
        --list.sz;
    }

    template<typename U>
    void push_front(U&& value){
        Node* node = list.createNode(std::forward<U>(value));
        list.fork(node, list.fakeNode.next);
        ++list.sz;
    }

    void pop_front(){
        Node* node = static_cast<Node*>(list.fakeNode.next);
        list.unfork(node);
        list.destroyNode(node);
        --list.sz;
    }

    template<typename U>
    iterator insert (const_iterator pos, U&& value){
        Node* node  = list.createNode(std::forward<U>(value));
        list.fork(node, pos.iterator_node);
        ++list.sz;
        return {node};
    }

    iterator erase (const_iterator pos){
        BaseNode* node = pos.iterator_node->next;
        list.unfork(pos.iterator_node);
        list.destroyNode(static_cast<Node*>(pos.iterator_node));
        --list.sz;
        return {node};
    }

    allocator_type get_allocator()const{
        return static_cast<NodeAlloc>(list);
    }

    size_t size()const{
        return list.sz;
    }

    bool empty()const{
        return (list.sz==0);
    }
};


